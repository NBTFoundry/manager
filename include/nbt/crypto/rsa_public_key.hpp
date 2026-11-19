// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CRYPTO_RSA_PUBLIC_KEY_HPP
#define NBT_CRYPTO_RSA_PUBLIC_KEY_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"

struct rsa_st;

namespace nbt {
namespace crypto {

class RsaPublicKey {
public:
    static core::Result<RsaPublicKey> fromPemFd(int fd);
    ~RsaPublicKey();
    RsaPublicKey(RsaPublicKey &&other);
    RsaPublicKey &operator=(RsaPublicKey &&other);
    RsaPublicKey(const RsaPublicKey &) = delete;
    RsaPublicKey &operator=(const RsaPublicKey &) = delete;

    core::Status verifySha256(core::Span<const uint8_t> digest,
                              core::Span<const uint8_t> signature) const;

private:
    explicit RsaPublicKey(rsa_st *key) : key_(key) {
    }
    rsa_st *key_;
};

} // namespace crypto
} // namespace nbt

#endif
