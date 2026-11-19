// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_TEST_PHONE_VECTORS_HPP
#define NBT_TEST_PHONE_VECTORS_HPP
#include "nbt/crypto/ec_public_key.hpp"
#include "nbt/util.h"
#include "nbt/worker/phone_auth.hpp"
#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
static bool signingVectors() {
    FILE *file = fopen("tests/phone-signing-vectors.tsv", "r");
    if (!file) {
        return false;
    }
    char line[4096];
    unsigned rows = 0;
    bool ok = true;
    while (ok && fgets(line, sizeof(line), file)) {
        if (line[0] == '#') {
            continue;
        }
        line[strcspn(line, "\n")] = 0;
        char *f[10];
        f[0] = line;
        unsigned count = 1;
        for (char *p = line; *p; ++p) {
            if (*p == '\t') {
                if (count == 10) {
                    ok = false;
                    break;
                }
                *p = 0;
                f[count++] = p + 1;
            }
        }
        if (!ok || count != 10) {
            ok = false;
            break;
        }
        unsigned char body[256], pub[65], sig[72], hash[32], digest[32], expected[32];
        char id[65], code[7];
        const size_t size = strlen(f[4]) / 2, sigSize = strlen(f[6]) / 2;
        ok = size <= sizeof(body) && sigSize <= sizeof(sig) && !nbt_hex_decode(f[4], body, size) &&
             !nbt_hex_decode(f[5], pub, 65) && !nbt_hex_decode(f[6], sig, sigSize) &&
             !nbt_hex_decode(f[7], expected, 32) && SHA256(body, size, hash) &&
             nbt::worker::phoneRequestDigest(f[0], f[1], f[2], f[3], size, hash, digest) &&
             !memcmp(digest, expected, 32) &&
             nbt::crypto::EcPublicKey::verify(pub, nbt::core::Span<const uint8_t>(digest, 32),
                                              nbt::core::Span<const uint8_t>(sig, sigSize)) &&
             nbt::worker::phonePairingCode(f[3], pub, code) && !strcmp(code, f[8]) &&
             nbt::crypto::EcPublicKey::id(pub, id) && !strcmp(id, f[9]);
        ++rows;
    }
    fclose(file);
    return ok && rows == 4;
}
#endif
