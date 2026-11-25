// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CRYPTO_RSA_PUBLIC_KEY_HPP
#define NBT_CRYPTO_RSA_PUBLIC_KEY_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"

#include <stddef.h>

struct rsa_st;

namespace nbt {
namespace crypto {

class RsaPublicKey {
public:
    static core::Result<RsaPublicKey> fromPemFd(int fd);
    /* An RSA-3072 key with exponent 65537 as its DER SubjectPublicKeyInfo
     * (`openssl pkey -pubin -outform DER`), which must be canonical: the key
     * written back is the same bytes. A developer key arrives this way. */
    static core::Result<RsaPublicKey> fromDer(core::Span<const uint8_t> der);
    static const size_t kDerMax = 600;
    ~RsaPublicKey();
    RsaPublicKey(RsaPublicKey &&other);
    RsaPublicKey &operator=(RsaPublicKey &&other);
    RsaPublicKey(const RsaPublicKey &) = delete;
    RsaPublicKey &operator=(const RsaPublicKey &) = delete;

    core::Status verifySha256(core::Span<const uint8_t> digest,
                              core::Span<const uint8_t> signature) const;
    /* The DER SubjectPublicKeyInfo, at most kDerMax bytes; *size its length. */
    core::Status der(core::Span<uint8_t> output, size_t *size) const;
    /* The key id: the SHA-256 of der() as 64 lowercase hex digits. */
    core::Status id(char output[65]) const;
    /* A seekable, read-only, close-on-exec descriptor of the key's PEM (an
     * unlinked temporary file), for nbt_package_verify. */
    core::Result<core::UniqueFd> pemFd() const;

private:
    explicit RsaPublicKey(rsa_st *key) : key_(key) {
    }
    rsa_st *key_;
};

} // namespace crypto
} // namespace nbt

#endif
