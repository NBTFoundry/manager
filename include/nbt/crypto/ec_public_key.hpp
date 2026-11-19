// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CRYPTO_EC_PUBLIC_KEY_HPP
#define NBT_CRYPTO_EC_PUBLIC_KEY_HPP
#include "nbt/core/span.hpp"
namespace nbt {
namespace crypto {
// SEC1 uncompressed P-256 point, exactly 65 bytes. No private key import.
class EcPublicKey {
public:
    static bool valid(const unsigned char key[65]);
    static bool id(const unsigned char key[65], char output[65]);
    static bool verify(const unsigned char key[65], core::Span<const uint8_t> digest,
                       core::Span<const uint8_t> signature);
};
} // namespace crypto
} // namespace nbt
#endif
