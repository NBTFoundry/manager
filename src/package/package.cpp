#include "nbt/package/package_verifier.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/crypto/sha256.hpp"
#include "nbt/model/bridge.hpp"
#include "nbt/package/manifest_scanner.hpp"
#include "nbt/sdk/settings.hpp"
#include "nbt/settings.h"
#include "nbt/util.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace package {
namespace {

const size_t kHeaderSize = 24;

core::Status failure(const char *message) {
    return core::Status::error(EINVAL, message);
}

core::Result<nbt_package> parseManifest(char *manifest, uint64_t payloadSize) {
    nbt_package out;
    memset(&out, 0, sizeof(out));
    out.payload_size = payloadSize;
    char *p = manifest;
    char *s = nbt_field(&p, "app");
    if (!nbt_token_valid(s, NBT_ID_MAX, 0)) {
        return core::Result<nbt_package>::failure(failure("invalid manifest"));
    }
    memcpy(out.app, s, strlen(s) + 1);
    s = nbt_field(&p, "package");
    unsigned char id[16];
    if (!s || nbt_hex_decode(s, id, sizeof(id))) {
        return core::Result<nbt_package>::failure(failure("invalid manifest"));
    }
    memcpy(out.package_id, s, strlen(s) + 1);
    s = nbt_field(&p, "version");
    if (!nbt_token_valid(s, NBT_VERSION_MAX, NBT_TOKEN_UPPER | NBT_TOKEN_DOT)) {
        return core::Result<nbt_package>::failure(failure("invalid manifest"));
    }
    memcpy(out.version, s, strlen(s) + 1);
    s = nbt_field(&p, "sequence");
    if (!s || nbt_parse_u64(s, &out.sequence, 0) || !out.sequence) {
        return core::Result<nbt_package>::failure(failure("invalid manifest"));
    }
    s = nbt_field(&p, "platform");
    if (!s || strcmp(s, "qnx6.5-armv7le")) {
        return core::Result<nbt_package>::failure(failure("invalid manifest"));
    }
    s = nbt_field(&p, "compat");
    if (!nbt_token_valid(s, NBT_ID_MAX, NBT_TOKEN_DOT)) {
        return core::Result<nbt_package>::failure(failure("invalid manifest"));
    }
    memcpy(out.compatibility, s, strlen(s) + 1);
    s = nbt_field(&p, "size");
    uint64_t size = 0;
    if (!s || nbt_parse_u64(s, &size, 0) || size != out.payload_size) {
        return core::Result<nbt_package>::failure(failure("invalid manifest"));
    }
    s = nbt_field(&p, "sha256");
    if (!s || nbt_hex_decode(s, out.payload_hash, 32) ||
        nbt_bridge_requirement_parse(&p, "", &out.bridge)) {
        return core::Result<nbt_package>::failure(failure("invalid manifest"));
    }
    while (*p) {
        s = nbt_field(&p, "setting");
        if (!s || nbt_settings_add(&out.settings, s)) {
            return core::Result<nbt_package>::failure(failure("invalid manifest"));
        }
    }
    return core::Result<nbt_package>::success(out);
}

} // namespace

#ifdef NBT_TESTING
core::Result<nbt_package> PackageVerifier::parseManifestForTesting(core::Span<const uint8_t> text,
                                                                   uint64_t payloadSize) {
    unsigned char manifest[NBT_MANIFEST_MAX + 1];
    if (text.size() > NBT_MANIFEST_MAX ||
        (text.size() && (!text.data() || memchr(text.data(), 0, text.size())))) {
        return core::Result<nbt_package>::failure(failure("invalid manifest encoding"));
    }
    if (text.size()) {
        memcpy(manifest, text.data(), text.size());
    }
    manifest[text.size()] = 0;
    return parseManifest(reinterpret_cast<char *>(manifest), payloadSize);
}
#endif

core::Result<nbt_package> PackageVerifier::peekApp(int fd) {
    unsigned char header[kHeaderSize];
    unsigned char manifest[NBT_MANIFEST_MAX + 1];
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        lseek(fd, 0, SEEK_SET) < 0 ||
        !PackageStream::readExact(fd, core::Span<uint8_t>(header)).ok() ||
        memcmp(header, "NBTMGR1\n", 8)) {
        return core::Result<nbt_package>::failure(failure("invalid package file"));
    }
    const uint32_t manifestSize = PackageStream::be32(header + 8);
    const uint32_t signatureSize = PackageStream::be32(header + 12);
    const uint64_t payloadSize = PackageStream::be64(header + 16);
    if (!manifestSize || manifestSize > NBT_MANIFEST_MAX || signatureSize != NBT_SIGNATURE_SIZE ||
        payloadSize < 52 || payloadSize > NBT_PAYLOAD_MAX ||
        static_cast<uint64_t>(st.st_size) !=
            kHeaderSize + manifestSize + signatureSize + payloadSize ||
        !PackageStream::readExact(fd, core::Span<uint8_t>(manifest, manifestSize)).ok() ||
        memchr(manifest, 0, manifestSize)) {
        return core::Result<nbt_package>::failure(failure("invalid package lengths"));
    }
    manifest[manifestSize] = 0;
    return parseManifest(reinterpret_cast<char *>(manifest), payloadSize);
}

core::Result<nbt_package> PackageVerifier::verify(int fd, int publicKeyFd, const char *appId,
                                                  const char *compatibility) {
    unsigned char header[kHeaderSize];
    core::SecureBytes<NBT_SIGNATURE_SIZE> signature;
    unsigned char manifest[NBT_MANIFEST_MAX + 1];
    core::SecureBytes<32> digest;
    core::SecureBytes<52> first;
    size_t firstSize = 0;
    struct stat st;
    if (fd < 0 || !appId || !compatibility || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || lseek(fd, 0, SEEK_SET) < 0 ||
        !PackageStream::readExact(fd, core::Span<uint8_t>(header)).ok()) {
        return core::Result<nbt_package>::failure(failure("invalid package file"));
    }
    if (memcmp(header, "NBTMGR1\n", 8)) {
        return core::Result<nbt_package>::failure(failure("unknown package format"));
    }
    const uint32_t manifestSize = PackageStream::be32(header + 8);
    const uint32_t signatureSize = PackageStream::be32(header + 12);
    const uint64_t payloadSize = PackageStream::be64(header + 16);
    if (!manifestSize || manifestSize > NBT_MANIFEST_MAX || signatureSize != NBT_SIGNATURE_SIZE ||
        payloadSize < 52 || payloadSize > NBT_PAYLOAD_MAX ||
        static_cast<uint64_t>(st.st_size) !=
            kHeaderSize + manifestSize + signatureSize + payloadSize) {
        return core::Result<nbt_package>::failure(failure("invalid package lengths"));
    }
    if (!PackageStream::readExact(fd, core::Span<uint8_t>(manifest, manifestSize)).ok() ||
        !PackageStream::readExact(fd, core::Span<uint8_t>(signature.data(), signature.size()))
             .ok()) {
        return core::Result<nbt_package>::failure(failure("truncated package"));
    }
    if (memchr(manifest, 0, manifestSize)) {
        return core::Result<nbt_package>::failure(failure("invalid manifest encoding"));
    }
    manifest[manifestSize] = 0;

    crypto::Sha256 packageSha;
    const core::Status sigStatus = PackageStream::verifySignature(
        publicKeyFd, false, core::Span<const uint8_t>(header),
        core::Span<const uint8_t>(manifest, manifestSize),
        core::Span<const uint8_t>(signature.data(), signature.size()), &packageSha,
        "package digest failed", "invalid package signature");
    if (!sigStatus.ok()) {
        return core::Result<nbt_package>::failure(sigStatus);
    }

    core::Result<nbt_package> parsed =
        parseManifest(reinterpret_cast<char *>(manifest), payloadSize);
    if (!parsed.ok()) {
        return parsed;
    }
    nbt_package out = parsed.value();
    if (strcmp(out.app, appId) || strcmp(out.compatibility, compatibility)) {
        return core::Result<nbt_package>::failure(failure("application or compatibility mismatch"));
    }
    out.payload_offset = static_cast<off_t>(kHeaderSize + manifestSize + signatureSize);

    const core::Status streamStatus = PackageStream::hashStream(
        fd, payloadSize, -1, core::Span<uint8_t>(first.data(), first.size()), &firstSize,
        &packageSha, core::Span<uint8_t>(digest.data(), digest.size()), "truncated payload",
        "package digest failed", NULL);
    if (!streamStatus.ok()) {
        return core::Result<nbt_package>::failure(streamStatus);
    }
    if (!packageSha.finish(core::Span<uint8_t>(out.package_hash, sizeof(out.package_hash))).ok()) {
        return core::Result<nbt_package>::failure(failure("package digest failed"));
    }
    if (memcmp(digest.data(), out.payload_hash, digest.size()) ||
        !PackageStream::validArmElf(core::Span<const uint8_t>(first.data(), firstSize), false)) {
        return core::Result<nbt_package>::failure(failure("invalid payload hash or ARM ELF"));
    }
    return core::Result<nbt_package>::success(out);
}

} // namespace package
} // namespace nbt

int nbt_package_peek_app(int fd, char app[NBT_ID_MAX + 1]) {
    if (!app) {
        return -1;
    }
    nbt::core::Result<nbt_package> result = nbt::package::PackageVerifier::peekApp(fd);
    if (!result.ok()) {
        return -1;
    }
    memcpy(app, result.value().app, strlen(result.value().app) + 1);
    return 0;
}

int nbt_package_verify(int fd, int publicKeyFd, const char *appId, const char *compatibility,
                       nbt_package *out, char *error, unsigned errorSize) {
    if (!out) {
        return nbt::package::PackageStream::legacyError(error, errorSize, "invalid package file");
    }
    memset(out, 0, sizeof(*out));
    nbt::core::Result<nbt_package> result =
        nbt::package::PackageVerifier::verify(fd, publicKeyFd, appId, compatibility);
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
