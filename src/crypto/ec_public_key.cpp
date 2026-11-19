#include "nbt/crypto/ec_public_key.hpp"
#include "nbt/core/text.hpp"
#include <memory>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <string.h>
namespace nbt {
namespace crypto {
namespace {
struct FreeKey {
    void operator()(EC_KEY *p) const {
        EC_KEY_free(p);
    }
};
struct FreePoint {
    void operator()(EC_POINT *p) const {
        EC_POINT_free(p);
    }
};
struct FreeSignature {
    void operator()(ECDSA_SIG *p) const {
        ECDSA_SIG_free(p);
    }
};
typedef std::unique_ptr<EC_KEY, FreeKey> Key;
Key importKey(const unsigned char bytes[65]) {
    Key key(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));
    if (!bytes || bytes[0] != 4 || !key) {
        return Key();
    }
    const EC_GROUP *group = EC_KEY_get0_group(key.get());
    std::unique_ptr<EC_POINT, FreePoint> point(EC_POINT_new(group));
    if (!point || EC_POINT_oct2point(group, point.get(), bytes, 65, NULL) != 1 ||
        EC_POINT_is_at_infinity(group, point.get()) != 0 ||
        EC_POINT_is_on_curve(group, point.get(), NULL) != 1 ||
        EC_KEY_set_public_key(key.get(), point.get()) != 1 || EC_KEY_check_key(key.get()) != 1) {
        return Key();
    }
    return key;
}
} // namespace
bool EcPublicKey::valid(const unsigned char key[65]) {
    return !!importKey(key);
}
bool EcPublicKey::id(const unsigned char key[65], char output[65]) {
    unsigned char digest[32];
    return key && output && SHA256(key, 65, digest) &&
           core::hexEncode(core::Span<const uint8_t>(digest, 32), core::Span<char>(output, 65))
               .ok();
}
bool EcPublicKey::verify(const unsigned char bytes[65], core::Span<const uint8_t> digest,
                         core::Span<const uint8_t> signature) {
    if (!digest.data() || digest.size() != 32 || !signature.data() || signature.size() < 8 ||
        signature.size() > 72) {
        return false;
    }
    Key key = importKey(bytes);
    if (!key) {
        return false;
    }
    const unsigned char *cursor = signature.data();
    std::unique_ptr<ECDSA_SIG, FreeSignature> parsed(
        d2i_ECDSA_SIG(NULL, &cursor, static_cast<long>(signature.size())));
    unsigned char canonical[72];
    unsigned char *out = canonical;
    if (!parsed || cursor != signature.data() + signature.size() ||
        i2d_ECDSA_SIG(parsed.get(), NULL) != static_cast<int>(signature.size()) ||
        i2d_ECDSA_SIG(parsed.get(), &out) != static_cast<int>(signature.size()) ||
        memcmp(canonical, signature.data(), signature.size())) {
        return false;
    }
    return ECDSA_do_verify(digest.data(), 32, parsed.get(), key.get()) == 1;
}
} // namespace crypto
} // namespace nbt
