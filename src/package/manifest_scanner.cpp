// SPDX-License-Identifier: MPL-2.0
#include "nbt/package/manifest_scanner.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/crypto/rsa_public_key.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace package {
namespace {

core::Status failure(const char *message) {
    return core::Status::error(EINVAL, message);
}

core::Result<crypto::RsaPublicKey> readKey(int fd, bool managerKey) {
    core::Result<crypto::RsaPublicKey> key = crypto::RsaPublicKey::fromPemFd(fd);
    if (key.ok()) {
        return key;
    }
    const char *message = key.status().message();
    if (managerKey) {
        if (!strcmp(message, "cannot seek public key") ||
            !strcmp(message, "cannot open public key")) {
            return core::Result<crypto::RsaPublicKey>::failure(failure("cannot open manager key"));
        }
        return core::Result<crypto::RsaPublicKey>::failure(failure("invalid manager key"));
    }
    if (!strcmp(message, "cannot seek public key")) {
        return core::Result<crypto::RsaPublicKey>::failure(failure("cannot seek trusted key"));
    }
    if (!strcmp(message, "cannot open public key")) {
        return core::Result<crypto::RsaPublicKey>::failure(failure("cannot open trusted key"));
    }
    return core::Result<crypto::RsaPublicKey>::failure(failure("invalid trusted RSA-3072 key"));
}

core::Status writeExact(int fd, const unsigned char *data, size_t size, const char *writeErr) {
    size_t written = 0;
    while (written < size) {
        const ssize_t result = write(fd, data + written, size - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return failure(writeErr);
        }
        written += static_cast<size_t>(result);
    }
    return core::Status();
}

} // namespace

uint32_t PackageStream::be32(const unsigned char *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint64_t PackageStream::be64(const unsigned char *p) {
    return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4);
}

core::Status PackageStream::readExact(int fd, core::Span<uint8_t> output) {
    size_t offset = 0;
    while (offset < output.size()) {
        const ssize_t count = read(fd, output.data() + offset, output.size() - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return core::Status::error(EIO, "truncated input");
        }
        offset += static_cast<size_t>(count);
    }
    return core::Status();
}

bool PackageStream::validArmElf(core::Span<const uint8_t> bytes, bool sharedLibraryOnly) {
    if (bytes.size() < 52 ||
        memcmp(bytes.data(),
               "\x7f"
               "ELF\x01\x01\x01",
               7) ||
        bytes[18] != 40 || bytes[19] != 0) {
        return false;
    }
    const unsigned type = bytes[16] | (static_cast<unsigned>(bytes[17]) << 8);
    return sharedLibraryOnly ? (type == 3) : (type == 2 || type == 3);
}

core::Status PackageStream::verifySignature(int keyFd, bool managerKey,
                                            core::Span<const uint8_t> header,
                                            core::Span<const uint8_t> manifest,
                                            core::Span<const uint8_t> signature,
                                            crypto::Sha256 *packageSha, const char *digestErr,
                                            const char *sigErr) {
    core::Result<crypto::RsaPublicKey> keyResult = readKey(keyFd, managerKey);
    if (!keyResult.ok()) {
        return keyResult.status();
    }
    const crypto::RsaPublicKey key = std::move(keyResult.value());
    core::SecureBytes<32> digest;
    crypto::Sha256 sha;
    if (!sha.update(header).ok() || !sha.update(manifest).ok() ||
        !sha.finish(core::Span<uint8_t>(digest.data(), digest.size())).ok()) {
        return failure(digestErr);
    }
    if (!key.verifySha256(core::Span<const uint8_t>(digest.data(), digest.size()), signature)
             .ok()) {
        return failure(sigErr);
    }
    if (!packageSha->update(header).ok() || !packageSha->update(manifest).ok() ||
        !packageSha->update(signature).ok()) {
        return failure(digestErr);
    }
    return core::Status();
}

core::Status PackageStream::hashStream(int fd, uint64_t size, int outputFd,
                                       core::Span<uint8_t> firstOut, size_t *firstSizeOut,
                                       crypto::Sha256 *packageSha, core::Span<uint8_t> digestOut,
                                       const char *readErr, const char *digestErr,
                                       const char *writeErr) {
    unsigned char buffer[8192];
    crypto::Sha256 sha;
    uint64_t remaining = size;
    if (firstSizeOut) {
        *firstSizeOut = 0;
    }
    while (remaining) {
        const size_t count =
            remaining < sizeof(buffer) ? static_cast<size_t>(remaining) : sizeof(buffer);
        if (!readExact(fd, core::Span<uint8_t>(buffer, count)).ok()) {
            return failure(readErr);
        }
        if (firstSizeOut && *firstSizeOut < firstOut.size()) {
            size_t copied = firstOut.size() - *firstSizeOut;
            if (copied > count) {
                copied = count;
            }
            memcpy(firstOut.data() + *firstSizeOut, buffer, copied);
            *firstSizeOut += copied;
        }
        if (!sha.update(core::Span<const uint8_t>(buffer, count)).ok() ||
            (packageSha && !packageSha->update(core::Span<const uint8_t>(buffer, count)).ok())) {
            return failure(digestErr);
        }
        if (outputFd >= 0) {
            const core::Status writeStatus = writeExact(outputFd, buffer, count, writeErr);
            if (!writeStatus.ok()) {
                return writeStatus;
            }
        }
        remaining -= count;
    }
    if (!sha.finish(digestOut).ok()) {
        return failure(digestErr);
    }
    return core::Status();
}

int PackageStream::legacyError(char *error, unsigned size, const char *message) {
    if (error && size) {
        snprintf(error, size, "%s", message);
    }
    return -1;
}

} // namespace package
} // namespace nbt
