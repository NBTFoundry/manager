// SPDX-License-Identifier: MPL-2.0
#include "nbt/crypto/rsa_public_key.hpp"

#include "nbt/core/unique_fd.hpp"
#include "nbt/crypto/sha256.hpp"
#include "nbt/fs.h"
#include "nbt/package.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace crypto {
namespace {

class KeyFile {
public:
    explicit KeyFile(FILE *file) : file_(file) {
    }
    ~KeyFile() {
        if (file_) {
            fclose(file_);
        }
    }
    FILE *get() const {
        return file_;
    }

private:
    FILE *file_;
};

bool strong(RSA *key) {
    const BIGNUM *n = NULL, *e = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    RSA_get0_key(key, &n, &e, NULL);
#else
    n = key->n;
    e = key->e;
#endif
    return n && e && BN_num_bits(n) == 3072 && BN_is_word(e, 65537) &&
           RSA_size(key) == NBT_SIGNATURE_SIZE;
}

} // namespace

core::Result<RsaPublicKey> RsaPublicKey::fromDer(core::Span<const uint8_t> der) {
    if (!der.data() || !der.size() || der.size() > kDerMax) {
        return core::Result<RsaPublicKey>::failure(
            core::Status::error(EINVAL, "invalid RSA-3072 public key"));
    }
    const unsigned char *cursor = der.data();
    RSA *key = d2i_RSA_PUBKEY(NULL, &cursor, static_cast<long>(der.size()));
    if (!key || cursor != der.data() + der.size() || !strong(key)) {
        if (key) {
            RSA_free(key);
        }
        return core::Result<RsaPublicKey>::failure(
            core::Status::error(EINVAL, "invalid RSA-3072 public key"));
    }
    RsaPublicKey result(key);
    uint8_t again[kDerMax];
    size_t size = 0;
    if (!result.der(core::Span<uint8_t>(again, sizeof(again)), &size).ok() || size != der.size() ||
        memcmp(again, der.data(), size)) {
        return core::Result<RsaPublicKey>::failure(
            core::Status::error(EINVAL, "public key is not canonical DER"));
    }
    return core::Result<RsaPublicKey>::success(static_cast<RsaPublicKey &&>(result));
}

core::Result<RsaPublicKey> RsaPublicKey::fromPemFd(int fd) {
    if (fd < 0 || lseek(fd, 0, SEEK_SET) < 0) {
        return core::Result<RsaPublicKey>::failure(
            core::Status::fromErrno("cannot seek public key"));
    }
    core::UniqueFd copy(dup(fd));
    if (!copy.valid()) {
        return core::Result<RsaPublicKey>::failure(
            core::Status::fromErrno("cannot open public key"));
    }
    KeyFile file(fdopen(copy.get(), "rb"));
    if (!file.get()) {
        return core::Result<RsaPublicKey>::failure(
            core::Status::fromErrno("cannot open public key"));
    }
    (void)copy.release();
    RSA *key = PEM_read_RSA_PUBKEY(file.get(), NULL, NULL, NULL);
    const bool valid = key && RSA_size(key) == NBT_SIGNATURE_SIZE
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
                       && RSA_bits(key) == 3072;
#else
                       && key->n && BN_num_bits(key->n) == 3072;
#endif
    if (!valid) {
        if (key) {
            RSA_free(key);
        }
        return core::Result<RsaPublicKey>::failure(
            core::Status::error(EINVAL, "invalid RSA-3072 public key"));
    }
    return core::Result<RsaPublicKey>::success(RsaPublicKey(key));
}

RsaPublicKey::~RsaPublicKey() {
    if (key_) {
        RSA_free(key_);
    }
}

RsaPublicKey::RsaPublicKey(RsaPublicKey &&other) : key_(other.key_) {
    other.key_ = NULL;
}

RsaPublicKey &RsaPublicKey::operator=(RsaPublicKey &&other) {
    if (this != &other) {
        if (key_) {
            RSA_free(key_);
        }
        key_ = other.key_;
        other.key_ = NULL;
    }
    return *this;
}

core::Status RsaPublicKey::verifySha256(core::Span<const uint8_t> digest,
                                        core::Span<const uint8_t> signature) const {
    if (!key_ || digest.size() != SHA256_DIGEST_LENGTH || signature.size() != NBT_SIGNATURE_SIZE) {
        return core::Status::error(EINVAL, "invalid signature input");
    }
    if (RSA_verify(NID_sha256, digest.data(), static_cast<unsigned>(digest.size()),
                   const_cast<unsigned char *>(signature.data()),
                   static_cast<unsigned>(signature.size()), key_) != 1) {
        return core::Status::error(EACCES, "invalid signature");
    }
    return core::Status();
}

core::Status RsaPublicKey::der(core::Span<uint8_t> output, size_t *size) const {
    if (!key_ || !output.data() || !size) {
        return core::Status::error(EINVAL, "invalid public key output");
    }
    const int needed = i2d_RSA_PUBKEY(key_, NULL);
    if (needed <= 0 || static_cast<size_t>(needed) > output.size()) {
        return core::Status::error(ERANGE, "public key too large");
    }
    unsigned char *cursor = output.data();
    if (i2d_RSA_PUBKEY(key_, &cursor) != needed) {
        return core::Status::error(EIO, "cannot encode public key");
    }
    *size = static_cast<size_t>(needed);
    return core::Status();
}

core::Status RsaPublicKey::id(char output[65]) const {
    uint8_t bytes[kDerMax], digest[32];
    size_t size = 0;
    core::Status status = der(core::Span<uint8_t>(bytes, sizeof(bytes)), &size);
    if (!status.ok()) {
        return status;
    }
    Sha256 sha;
    if (!sha.update(core::Span<const uint8_t>(bytes, size)).ok() ||
        !sha.finish(core::Span<uint8_t>(digest, sizeof(digest))).ok() ||
        nbt_hex(digest, sizeof(digest), output, 65)) {
        return core::Status::error(EIO, "cannot hash public key");
    }
    return core::Status();
}

core::Result<core::UniqueFd> RsaPublicKey::pemFd() const {
    FILE *file = key_ ? nbt_fs_bootstrap_temp() : NULL; // private, unlinked, close-on-exec
    if (!file) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::error(key_ ? errno : EINVAL, "cannot write public key"));
    }
    core::UniqueFd fd(dup(fileno(file)));
    const bool written = PEM_write_RSA_PUBKEY(file, key_) == 1 && !fflush(file);
    fclose(file);
    if (!fd.valid() || !written || fcntl(fd.get(), F_SETFD, FD_CLOEXEC) ||
        lseek(fd.get(), 0, SEEK_SET)) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::error(EIO, "cannot write public key"));
    }
    return core::Result<core::UniqueFd>::success(static_cast<core::UniqueFd &&>(fd));
}

} // namespace crypto
} // namespace nbt
