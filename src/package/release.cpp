// SPDX-License-Identifier: MPL-2.0
#include "nbt/package/release_verifier.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/crypto/sha256.hpp"
#include "nbt/package.h"
#include "nbt/package/manifest_scanner.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace package {
namespace {

const size_t kHeaderSize = 16;
const char *const kNames[NBT_RELEASE_MAX_COMPONENTS] = {
    "launcher",   "gui",         "worker",        "plugin_lib", "plugin_manifest",
    "plugin_xml", "plugin_text", "plugin_images", "capture_lib"};

bool isElfComponent(unsigned index) {
    return index < 4 || index == NBT_RELEASE_CAPTURE_INDEX;
}

bool mustBeSharedObject(unsigned index) {
    return index == 3 || index == NBT_RELEASE_CAPTURE_INDEX;
}

core::Status failure(const char *message) {
    return core::Status::error(EINVAL, message);
}

core::Status parseManifest(char *text, unsigned componentCount, nbt_release *out, uint64_t *total) {
    char *p = text;
    char *s = nbt_field(&p, "version");
    if (!nbt_token_valid(s, 48, NBT_TOKEN_UPPER | NBT_TOKEN_DOT)) {
        return failure("invalid manifest");
    }
    memcpy(out->version, s, strlen(s) + 1);
    s = nbt_field(&p, "sequence");
    if (!s || nbt_parse_u64(s, &out->sequence, 0) || !out->sequence) {
        return failure("invalid manifest");
    }
    s = nbt_field(&p, "platform");
    if (!s || strcmp(s, "qnx6.5-armv7le")) {
        return failure("invalid manifest");
    }
    s = nbt_field(&p, "compat");
    if (!nbt_token_valid(s, 48, NBT_TOKEN_DOT)) {
        return failure("invalid manifest");
    }
    memcpy(out->compatibility, s, strlen(s) + 1);
    while (!strncmp(p, "bridge=", 7)) {
        s = nbt_field(&p, "bridge");
        if (!s || nbt_bridge_provider_add(&out->bridge, s)) {
            return failure("invalid manifest");
        }
    }
    *total = 0;
    for (unsigned i = 0; i < componentCount; ++i) {
        char field[32];
        uint64_t size = 0;
        if (snprintf(field, sizeof(field), "%s_size", kNames[i]) >=
            static_cast<int>(sizeof(field))) {
            return failure("invalid manifest");
        }
        s = nbt_field(&p, field);
        if (!s || nbt_parse_u64(s, &size, 0) || !size || size > NBT_PAYLOAD_MAX ||
            (isElfComponent(i) && size < 52) || *total > NBT_RELEASE_PAYLOAD_MAX - size) {
            return failure("invalid manifest");
        }
        out->components[i].size = size;
        *total += size;
        if (snprintf(field, sizeof(field), "%s_sha256", kNames[i]) >=
            static_cast<int>(sizeof(field))) {
            return failure("invalid manifest");
        }
        s = nbt_field(&p, field);
        if (!s || nbt_hex_decode(s, out->components[i].sha256, 32)) {
            return failure("invalid manifest");
        }
    }
    return *p ? failure("invalid manifest") : core::Status();
}

} // namespace

const char *componentName(unsigned index) {
    return index < NBT_RELEASE_MAX_COMPONENTS ? kNames[index] : NULL;
}

#ifdef NBT_TESTING
core::Status SignedReleaseVerifier::parseManifestForTesting(core::Span<const uint8_t> text,
                                                            unsigned components) {
    unsigned char manifest[NBT_RELEASE_MANIFEST_MAX + 1];
    nbt_release out;
    uint64_t total = 0;
    memset(&out, 0, sizeof(out));
    if (components != NBT_RELEASE_V1_COMPONENTS && components != NBT_RELEASE_MAX_COMPONENTS) {
        return failure("invalid component count");
    }
    if (text.size() > NBT_RELEASE_MANIFEST_MAX ||
        (text.size() && (!text.data() || memchr(text.data(), 0, text.size())))) {
        return failure("invalid manifest encoding");
    }
    if (text.size()) {
        memcpy(manifest, text.data(), text.size());
    }
    manifest[text.size()] = 0;
    return parseManifest(reinterpret_cast<char *>(manifest), components, &out, &total);
}
#endif

core::Result<nbt_release> SignedReleaseVerifier::verify(int fd, const char *compatibility) {
    unsigned char header[kHeaderSize];
    core::SecureBytes<NBT_SIGNATURE_SIZE> signature;
    unsigned char manifest[NBT_RELEASE_MANIFEST_MAX + 1];
    core::SecureBytes<32> digest;
    core::SecureBytes<52> first;
    struct stat st;
    if (!compatibility) {
        return core::Result<nbt_release>::failure(failure("invalid arguments"));
    }
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        lseek(fd, 0, SEEK_SET) < 0 ||
        !PackageStream::readExact(fd, core::Span<uint8_t>(header)).ok()) {
        return core::Result<nbt_release>::failure(failure("invalid release header"));
    }
    uint8_t formatVersion = 0;
    uint8_t componentCount = 0;
    if (!memcmp(header, "NBTRLS1\n", 8)) {
        formatVersion = 1;
        componentCount = NBT_RELEASE_V1_COMPONENTS;
    } else if (!memcmp(header, "NBTRLS2\n", 8)) {
        formatVersion = 2;
        componentCount = NBT_RELEASE_MAX_COMPONENTS;
    } else {
        return core::Result<nbt_release>::failure(failure("invalid release header"));
    }
    const uint32_t manifestSize = PackageStream::be32(header + 8);
    if (!manifestSize || manifestSize > NBT_RELEASE_MANIFEST_MAX ||
        PackageStream::be32(header + 12) != NBT_SIGNATURE_SIZE ||
        static_cast<uint64_t>(st.st_size) < kHeaderSize + manifestSize + NBT_SIGNATURE_SIZE ||
        !PackageStream::readExact(fd, core::Span<uint8_t>(manifest, manifestSize)).ok() ||
        !PackageStream::readExact(fd, core::Span<uint8_t>(signature.data(), signature.size()))
             .ok() ||
        memchr(manifest, 0, manifestSize)) {
        return core::Result<nbt_release>::failure(failure("invalid release lengths"));
    }
    manifest[manifestSize] = 0;

    crypto::Sha256 packageSha;
    const core::Status sigStatus = PackageStream::verifySignature(
        managerKeyFd_, true, core::Span<const uint8_t>(header),
        core::Span<const uint8_t>(manifest, manifestSize),
        core::Span<const uint8_t>(signature.data(), signature.size()), &packageSha,
        "release digest failed", "invalid release signature");
    if (!sigStatus.ok()) {
        return core::Result<nbt_release>::failure(sigStatus);
    }

    nbt_release out;
    memset(&out, 0, sizeof(out));
    out.format_version = formatVersion;
    out.component_count = componentCount;
    uint64_t total = 0;
    if (!parseManifest(reinterpret_cast<char *>(manifest), componentCount, &out, &total).ok() ||
        strcmp(compatibility, out.compatibility) ||
        static_cast<uint64_t>(st.st_size) !=
            kHeaderSize + manifestSize + NBT_SIGNATURE_SIZE + total) {
        return core::Result<nbt_release>::failure(
            failure("invalid manifest or incompatible release"));
    }

    for (unsigned i = 0; i < componentCount; ++i) {
        size_t firstSize = 0;
        out.components[i].offset = lseek(fd, 0, SEEK_CUR);
        if (out.components[i].offset < 0) {
            return core::Result<nbt_release>::failure(failure("cannot seek release"));
        }
        const bool elf = isElfComponent(i);
        const core::Status streamStatus = PackageStream::hashStream(
            fd, out.components[i].size, -1, core::Span<uint8_t>(first.data(), first.size()),
            elf ? &firstSize : NULL, &packageSha, core::Span<uint8_t>(digest.data(), digest.size()),
            "truncated release", "release digest failed", NULL);
        if (!streamStatus.ok()) {
            return core::Result<nbt_release>::failure(streamStatus);
        }
        if (!nbt_constant_equal(digest.data(), out.components[i].sha256, digest.size()) ||
            (elf && !PackageStream::validArmElf(core::Span<const uint8_t>(first.data(), firstSize),
                                                mustBeSharedObject(i)))) {
            return core::Result<nbt_release>::failure(failure("invalid component hash or ARM ELF"));
        }
    }
    if (!packageSha.finish(core::Span<uint8_t>(out.sha256, sizeof(out.sha256))).ok()) {
        return core::Result<nbt_release>::failure(failure("release digest failed"));
    }
    return core::Result<nbt_release>::success(out);
}

core::Status SignedReleaseVerifier::copyComponent(int fd, const nbt_release &verified,
                                                  unsigned index, int outputFd) {
    core::SecureBytes<52> first;
    core::SecureBytes<32> digest;
    size_t firstSize = 0;
    struct stat source, destination;
    if (verified.component_count > NBT_RELEASE_MAX_COMPONENTS ||
        index >= verified.component_count) {
        return failure("invalid component");
    }
    const nbt_release_component &component = verified.components[index];
    const bool elf = isElfComponent(index);
    if (component.offset < 0 || !component.size || component.size > NBT_PAYLOAD_MAX ||
        (elf && component.size < first.size()) || fstat(fd, &source) || !S_ISREG(source.st_mode) ||
        source.st_size < 0 ||
        static_cast<uint64_t>(component.offset) > static_cast<uint64_t>(source.st_size) ||
        component.size >
            static_cast<uint64_t>(source.st_size) - static_cast<uint64_t>(component.offset) ||
        fstat(outputFd, &destination) || !S_ISREG(destination.st_mode) ||
        destination.st_nlink != 1 || destination.st_size != 0 ||
        (source.st_dev == destination.st_dev && source.st_ino == destination.st_ino) ||
        lseek(fd, component.offset, SEEK_SET) < 0 || lseek(outputFd, 0, SEEK_SET) < 0) {
        return failure("invalid component source or destination");
    }
    const core::Status streamStatus = PackageStream::hashStream(
        fd, component.size, outputFd, core::Span<uint8_t>(first.data(), first.size()),
        elf ? &firstSize : NULL, NULL, core::Span<uint8_t>(digest.data(), digest.size()),
        "component read failed", "component digest failed", "component write failed");
    if (!streamStatus.ok()) {
        return streamStatus;
    }
    if (!nbt_constant_equal(digest.data(), component.sha256, digest.size()) ||
        (elf && !PackageStream::validArmElf(core::Span<const uint8_t>(first.data(), firstSize),
                                            mustBeSharedObject(index)))) {
        return failure("component changed after verification");
    }
    return core::Status();
}

} // namespace package
} // namespace nbt

const char *nbt_release_component_name(unsigned index) {
    return nbt::package::componentName(index);
}

extern "C" int nbt_release_verify(int fd, int managerKeyFd, const char *compatibility,
                                  nbt_release *out, char *error, unsigned errorSize) {
    if (!out) {
        return nbt::package::PackageStream::legacyError(error, errorSize, "invalid arguments");
    }
    memset(out, 0, sizeof(*out));
    nbt::package::SignedReleaseVerifier verifier(managerKeyFd);
    nbt::core::Result<nbt_release> result = verifier.verify(fd, compatibility);
    if (!result.ok()) {
        return nbt::package::PackageStream::legacyError(error, errorSize,
                                                        result.status().message());
    }
    *out = result.value();
    if (error && errorSize) {
        error[0] = 0;
    }
    return 0;
}

int nbt_release_copy_component(int fd, const nbt_release *verified, unsigned index, int outputFd,
                               char *error, unsigned errorSize) {
    if (!verified) {
        return nbt::package::PackageStream::legacyError(error, errorSize, "invalid component");
    }
    const nbt::core::Status status =
        nbt::package::SignedReleaseVerifier::copyComponent(fd, *verified, index, outputFd);
    if (!status.ok()) {
        return nbt::package::PackageStream::legacyError(error, errorSize, status.message());
    }
    if (error && errorSize) {
        error[0] = 0;
    }
    return 0;
}
