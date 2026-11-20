// SPDX-License-Identifier: MPL-2.0
#include "nbt/core/text.hpp"
#include "nbt/crypto/ec_public_key.hpp"
#include "nbt/gui/phone_wake.hpp"
#include "nbt/worker/phone_auth.hpp"
#include "phone_vectors.hpp"
#include "support/check.hpp"
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <string.h>
#include <unistd.h>
using nbt::core::Span;
using nbt::crypto::EcPublicKey;
static bool run() {
    EC_KEY *key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    TEST_CHECK(key && EC_KEY_generate_key(key) == 1);
    unsigned char pub[65], digest[32], changed[32], signature[72];
    unsigned sigSize = sizeof(signature);
    TEST_CHECK(EC_POINT_point2oct(EC_KEY_get0_group(key), EC_KEY_get0_public_key(key),
                                  POINT_CONVERSION_UNCOMPRESSED, pub, 65, NULL) == 65);
    TEST_CHECK(EcPublicKey::valid(pub));
    char id[65];
    TEST_CHECK(EcPublicKey::id(pub, id) && strlen(id) == 64);
    TEST_CHECK(SHA256(reinterpret_cast<const unsigned char *>("body"), 4, digest));
    TEST_CHECK(ECDSA_sign(0, digest, 32, signature, &sigSize, key) == 1);
    TEST_CHECK(EcPublicKey::verify(pub, Span<const uint8_t>(digest, 32),
                                   Span<const uint8_t>(signature, sigSize)));
    memcpy(changed, digest, 32);
    changed[0] ^= 1;
    TEST_CHECK(!EcPublicKey::verify(pub, Span<const uint8_t>(changed, 32),
                                    Span<const uint8_t>(signature, sigSize)));
    TEST_CHECK(!EcPublicKey::verify(pub, Span<const uint8_t>(digest, 31),
                                    Span<const uint8_t>(signature, sigSize)));
    pub[0] = 3;
    TEST_CHECK(!EcPublicKey::valid(pub));
    pub[0] = 4;
    EC_KEY_free(key);
    nbt::worker::PhoneNonces nonces;
    char nonce[65];
    TEST_CHECK(nonces.issue(10, nonce));
    TEST_CHECK(!nonces.take("bad", 11));
    TEST_CHECK(nonces.take(nonce, 11));
    TEST_CHECK(!nonces.take(nonce, 11));
    TEST_CHECK(nonces.issue(10, nonce));
    TEST_CHECK(!nonces.take(nonce, 60010));
    for (unsigned i = 0; i < 120; ++i) {
        TEST_CHECK(nonces.signatureAllowed(100));
    }
    TEST_CHECK(!nonces.signatureAllowed(100));
    TEST_CHECK(nonces.signatureAllowed(60100));
    nbt::worker::PhoneNonces full;
    for (unsigned i = 0; i < 32; ++i) {
        TEST_CHECK(full.issue(1, nonce));
    }
    TEST_CHECK(!full.issue(1, nonce));
    TEST_CHECK(full.issue(60001, nonce));
    TEST_CHECK(!full.issue(UINT64_MAX, nonce));
    memset(nonce, '0', 64);
    nonce[64] = 0;
    unsigned char body[32];
    TEST_CHECK(SHA256(NULL, 0, body));
    TEST_CHECK(nbt::worker::phoneRequestDigest("GET", "/v1/apps", "172.16.222.1:8081", nonce, 0,
                                               body, digest));
    const char canonical[] = "NBT-REQ-1\nGET\n/v1/apps\n172.16.222.1:8081\n"
                             "0000000000000000000000000000000000000000000000000000000000000000\n0\n"
                             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n";
    TEST_CHECK(
        SHA256(reinterpret_cast<const unsigned char *>(canonical), strlen(canonical), changed));
    TEST_CHECK(!memcmp(digest, changed, 32));
    TEST_CHECK(
        !nbt::worker::phoneRequestDigest("GET", "/v1/apps\n", "host", nonce, 0, body, digest));
    TEST_CHECK(
        !nbt::worker::phoneRequestDigest("DELETE", "/v1/apps", "host", nonce, 0, body, digest));
    char code[7];
    TEST_CHECK(nbt::worker::phonePairingCode(nonce, pub, code) && strlen(code) == 6);
    TEST_CHECK(signingVectors());
    using namespace nbt::gui;
    TEST_CHECK(phoneWakeRule(false, true, 0, 100, true, true, 100, false, 100) == WakeOff);
    TEST_CHECK(phoneWakeRule(true, false, 0, 100, true, true, 100, false, 100) == WakeMoving);
    TEST_CHECK(phoneWakeRule(true, true, 1, 100, true, true, 100, false, 100) == WakeMoving);
    TEST_CHECK(phoneWakeRule(true, true, 0, 100, true, true, 100, false, 3101) == WakeMoving);
    TEST_CHECK(phoneWakeRule(true, true, 0, 100, false, true, 100, false, 100) == WakeBusy);
    TEST_CHECK(phoneWakeRule(true, true, 0, 100, true, false, 100, false, 100) == WakeBusy);
    TEST_CHECK(phoneWakeRule(true, true, 0, 100, true, true, 100, false, 100) == WakeOpening);
    TEST_CHECK(phoneWakeRule(true, true, 0, 100, false, false, 0, true, 100) == WakeOpen);
    unsigned speed = 7;
    TEST_CHECK(phoneSpeed("{\"speedActual\":0}", speed) && speed == 0);
    TEST_CHECK(!phoneSpeed("{\"speedActual\":-1}", speed));
    TEST_CHECK(!phoneSpeed("{\"speedActual\":00}", speed));
    TEST_CHECK(!phoneSpeed("{\"speedActual\":65536}", speed));
    TEST_CHECK(!phoneSpeed("{\"speedActual\":18446744073709551616}", speed));
    TEST_CHECK(phoneSpeed("{\"speedActual\":65535}", speed) && speed == 65535);
    PhoneWake parsed;
    const char maxWake[] = "NBTWAKE1\nid=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                           "when=18446744073709551615\nstate=4\n";
    TEST_CHECK(phoneWakeParse(maxWake, sizeof(maxWake) - 1, parsed));
    TEST_CHECK(parsed.when == UINT64_MAX && parsed.state == WakeOff);
    const char overflowWake[] = "NBTWAKE1\nid=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
                                "when=18446744073709551616\nstate=4\n";
    TEST_CHECK(!phoneWakeParse(overflowWake, sizeof(overflowWake) - 1, parsed));
    TEST_CHECK(!phoneWakeParse(maxWake, sizeof(maxWake), parsed));
    TEST_CHECK(!phoneWakeParse(maxWake, sizeof(maxWake) - 2, parsed));
    TEST_CHECK(!phoneContextSafe(
        "{\"graphicalContext\":{\"visibleContext\":692,\"widgetType\":\"Camera\"}}"));
    TEST_CHECK(phoneContextSafe(
        "{\"graphicalContext\":{\"visibleContext\":3,\"focusIndex\":0,\"listIndex\":-1,"
        "\"widgetType\":\"LT_Table_ID6L_Pad_Title\",\"enabled\":\"1\"}}"));
    char path[] = "/tmp/nbt-phone-wake-XXXXXX";
    int fd = mkstemp(path);
    TEST_CHECK(fd >= 0);
    close(fd);
    PhoneWake wake = {{}, 123, WakeOpening}, readWake;
    memset(wake.id, 'a', 32);
    wake.id[32] = 0;
    TEST_CHECK(phoneWakeWrite(path, wake) && phoneWakeRead(path, readWake));
    TEST_CHECK(readWake.when == 123 && !strcmp(readWake.id, wake.id));
    unlink(path);
    TEST_CHECK(phoneDoorClaim(path) && phoneDoorClaim(path) && phoneDoorClaim(path) &&
               !phoneDoorClaim(path));
    unlink(path);
    // Automatic starts: asked for once per boot (the record outlives an iDrive restart).
    TEST_CHECK(bootOnceClaim(path) && !bootOnceClaim(path) && !bootOnceClaim(path));
    unlink(path);
    TEST_CHECK(symlink("/dev/null", path) == 0 && !bootOnceClaim(path));
    unlink(path);
    return true;
}
int main() {
    TEST_RUN(run);
    return 0;
}
