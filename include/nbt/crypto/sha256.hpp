#ifndef NBT_CRYPTO_SHA256_HPP
#define NBT_CRYPTO_SHA256_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"

#include <openssl/sha.h>

namespace nbt {
namespace crypto {

class Sha256 {
public:
    Sha256();
    Sha256(const Sha256 &) = delete;
    Sha256 &operator=(const Sha256 &) = delete;

    core::Status status() const {
        return status_;
    }
    core::Status update(core::Span<const uint8_t> bytes);
    core::Status finish(core::Span<uint8_t> digest);

private:
    SHA256_CTX context_;
    core::Status status_;
    bool finished_;
};

} // namespace crypto
} // namespace nbt

#endif
