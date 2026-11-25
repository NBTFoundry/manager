// SPDX-License-Identifier: MPL-2.0
// Developer keys a paired phone adds or removes (/v1/keys/add, /v1/keys/remove),
// each confirmed on the car like a pairing (PROTOCOL.md, "Developer keys").
#include "nbt/crypto/rsa_public_key.hpp"
#include "nbt/util.h"
#include "nbt/worker/phone_actions.hpp"
#include <stdio.h>
#include <string.h>
#include <time.h>
namespace nbt {
namespace worker {
bool PhoneActions::parseKey(const char *body, storage::Developer &developer) {
    char copy[1400];
    if (!body || strlen(body) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, body);
    if (strncmp(copy, "NBTKEY1\n", 8)) {
        return false;
    }
    char *cursor = copy + 8;
    char *name = nbt_field(&cursor, "name"), *key = nbt_field(&cursor, "key");
    unsigned char der[crypto::RsaPublicKey::kDerMax];
    const size_t hexSize = key ? strlen(key) : 0;
    if (!storage::PhoneStore::nameValid(name) || !hexSize || hexSize % 2 ||
        hexSize / 2 > sizeof(der) || *cursor || nbt_hex_decode(key, der, hexSize / 2) ||
        !storage::DeveloperStore::fromDer(der, hexSize / 2, developer)) {
        return false;
    }
    for (size_t i = 0; i < hexSize; ++i) {
        if (key[i] >= 'A' && key[i] <= 'F') {
            return false;
        }
    }
    strcpy(developer.name, name);
    return true;
}
int PhoneActions::addKey(const char *key, const char *body, uint64_t now, core::Span<char> out) {
    storage::Developer proposed, existing[storage::DeveloperStore::kMax];
    if (!parseKey(body, proposed)) {
        return 400;
    }
    const int count = developers_.list(existing);
    if (count < 0 || count >= storage::DeveloperStore::kMax) {
        return 409;
    }
    for (int i = 0; i < count; ++i) {
        if (!strcmp(existing[i].id, proposed.id)) {
            return 409;
        }
    }
    const time_t epoch = time(NULL);
    if (epoch <= 0) {
        return 503;
    }
    proposed.added = static_cast<uint64_t>(epoch);
    if (!begin(key, "key", "Confirm the key on the car", now)) {
        return 503;
    }
    pendingApp_[0] = 0;
    keyRemove_[0] = 0;
    keyCandidate_ = proposed;
    snprintf(out.data(), out.size(), "STATUS\twaiting\tConfirm the key on the car\n");
    return 202;
}
int PhoneActions::removeKey(const char *key, const char *body, uint64_t now, core::Span<char> out) {
    storage::Developer developer;
    char id[65];
    const size_t size = strlen(body);
    if (size != 65 || body[64] != '\n') {
        return 400;
    }
    memcpy(id, body, 64);
    id[64] = 0;
    for (size_t i = 0; i < 64; ++i) {
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) {
            return 400;
        }
    }
    if (!developers_.find(id, developer)) {
        return 404;
    }
    if (!begin(key, "unkey", "Confirm on the car", now)) {
        return 503;
    }
    pendingApp_[0] = 0;
    keyCandidate_ = storage::Developer();
    strcpy(keyRemove_, id);
    snprintf(out.data(), out.size(), "STATUS\twaiting\tConfirm on the car\n");
    return 202;
}
} // namespace worker
} // namespace nbt
