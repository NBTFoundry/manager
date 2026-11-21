// SPDX-License-Identifier: MPL-2.0
#include "nbt/package/release_verifier.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/crypto/sha256.hpp"
#include "nbt/package.h"
#include "nbt/package/manifest_scanner.hpp"
#include "nbt/package/release_manifest.hpp"
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
core::Status failure(const char *message) {
    return core::Status::error(EINVAL, message);
}

} // namespace

int findComponent(const nbt_release &release, const char *name) {
    for (unsigned i = 0; name && i < release.component_count && i < NBT_RELEASE_MAX_COMPONENTS;
         ++i) {
        if (!strcmp(release.components[i].name, name)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

#ifdef NBT_TESTING
core::Status SignedReleaseVerifier::parseManifestForTesting(core::Span<const uint8_t> text,
                                                            unsigned format) {
    unsigned char manifest[NBT_RELEASE_MANIFEST_MAX + 1];
    nbt_release out;
    uint64_t total = 0;
    memset(&out, 0, sizeof(out));
    if (format < 1 || format > 3) {
        return failure("invalid format");
    }
    if (text.size() > NBT_RELEASE_MANIFEST_MAX ||
        (text.size() && (!text.data() || memchr(text.data(), 0, text.size())))) {
        return failure("invalid manifest encoding");
    }
    if (text.size()) {
        memcpy(manifest, text.data(), text.size());
    }
    manifest[text.size()] = 0;
    return parseManifest(reinterpret_cast<char *>(manifest), static_cast<uint8_t>(format), &out,
                         &total);
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
    if (!memcmp(header, "NBTRLS", 6) && header[7] == '\n' && header[6] >= '1' && header[6] <= '3') {
        formatVersion = static_cast<uint8_t>(header[6] - '0');
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
    uint64_t total = 0;
    if (!parseManifest(reinterpret_cast<char *>(manifest), formatVersion, &out, &total).ok() ||
        strcmp(compatibility, out.compatibility) ||
        static_cast<uint64_t>(st.st_size) !=
            kHeaderSize + manifestSize + NBT_SIGNATURE_SIZE + total) {
        return core::Result<nbt_release>::failure(
            failure("invalid manifest or incompatible release"));
    }

    for (unsigned i = 0; i < out.component_count; ++i) {
        size_t firstSize = 0;
        out.components[i].offset = lseek(fd, 0, SEEK_CUR);
        if (out.components[i].offset < 0) {
            return core::Result<nbt_release>::failure(failure("cannot seek release"));
        }
        const bool elf = isElfComponent(out, i);
        const core::Status streamStatus = PackageStream::hashStream(
            fd, out.components[i].size, -1, core::Span<uint8_t>(first.data(), first.size()),
            elf ? &firstSize : NULL, &packageSha, core::Span<uint8_t>(digest.data(), digest.size()),
            "truncated release", "release digest failed", NULL);
        if (!streamStatus.ok()) {
            return core::Result<nbt_release>::failure(streamStatus);
        }
        if (!nbt_constant_equal(digest.data(), out.components[i].sha256, digest.size()) ||
            (elf && !PackageStream::validArmElf(core::Span<const uint8_t>(first.data(), firstSize),
                                                mustBeSharedObject(out, i)))) {
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
    const bool elf = isElfComponent(verified, index);
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
                                            mustBeSharedObject(verified, index)))) {
        return failure("component changed after verification");
    }
    return core::Status();
}

} // namespace package
} // namespace nbt

int nbt_release_find(const nbt_release *release, const char *name) {
    return release ? nbt::package::findComponent(*release, name) : -1;
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
