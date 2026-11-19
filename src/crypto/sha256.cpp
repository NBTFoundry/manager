#include "nbt/crypto/sha256.hpp"

#include <errno.h>

namespace nbt {
namespace crypto {

Sha256::Sha256() : context_(), status_(), finished_(false) {
    if (SHA256_Init(&context_) != 1) {
        status_ = core::Status::error(EIO, "cannot initialize SHA-256");
    }
}

core::Status Sha256::update(core::Span<const uint8_t> bytes) {
    if (!status_.ok() || finished_) {
        return core::Status::error(EINVAL, "invalid SHA-256 state");
    }
    if (bytes.size() && SHA256_Update(&context_, bytes.data(), bytes.size()) != 1) {
        status_ = core::Status::error(EIO, "cannot update SHA-256");
    }
    return status_;
}

core::Status Sha256::finish(core::Span<uint8_t> digest) {
    if (digest.size() != SHA256_DIGEST_LENGTH) {
        return core::Status::error(EINVAL, "invalid SHA-256 output size");
    }
    if (!status_.ok() || finished_) {
        return core::Status::error(EINVAL, "invalid SHA-256 state");
    }
    if (SHA256_Final(digest.data(), &context_) != 1) {
        status_ = core::Status::error(EIO, "cannot finish SHA-256");
        return status_;
    }
    finished_ = true;
    return status_;
}

} // namespace crypto
} // namespace nbt
