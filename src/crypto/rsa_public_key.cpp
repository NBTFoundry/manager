#include "nbt/crypto/rsa_public_key.hpp"

#include "nbt/core/unique_fd.hpp"
#include "nbt/package.h"

#include <errno.h>
#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
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

} // namespace

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

} // namespace crypto
} // namespace nbt
