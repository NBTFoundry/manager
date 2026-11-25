// SPDX-License-Identifier: MPL-2.0
#include "nbt/diag_policy.h"

#include <string.h>

namespace {

const struct {
    const char *name;
    unsigned bit;
} kClasses[] = {{"read", NBT_DIAG_READ},
                {"codes", NBT_DIAG_CODES},
                {"clear", NBT_DIAG_CLEAR},
                {"control", NBT_DIAG_CONTROL},
                {"drive", NBT_DIAG_DRIVE}};

int hexDigit(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// "read+codes": the classes in their canonical order, each once.
bool parseClasses(const char *p, size_t n, uint8_t *out) {
    unsigned bits = 0, next = 0;
    while (n) {
        const char *plus = static_cast<const char *>(memchr(p, '+', n));
        const size_t len = plus ? static_cast<size_t>(plus - p) : n;
        bool found = false;
        for (unsigned i = next; i < sizeof(kClasses) / sizeof(kClasses[0]); ++i) {
            if (strlen(kClasses[i].name) == len && !memcmp(kClasses[i].name, p, len)) {
                bits |= kClasses[i].bit;
                next = i + 1;
                found = true;
                break;
            }
        }
        if (!found || (plus && len + 1 == n)) {
            return false;
        }
        n -= plus ? len + 1 : len;
        p += plus ? len + 1 : len;
    }
    *out = static_cast<uint8_t>(bits);
    // `drive` modifies clear or control.
    return bits != 0 && (!(bits & NBT_DIAG_DRIVE) || (bits & (NBT_DIAG_CLEAR | NBT_DIAG_CONTROL)));
}

} // namespace

int nbt_diag_policy_parse(const char *text, nbt_diag_policy *out) {
    nbt_diag_policy policy;
    if (!text || !memchr(text, 0, NBT_DIAG_POLICY_MAX + 1) || !*text) {
        return -1;
    }
    memset(&policy, 0, sizeof(policy));
    for (const char *p = text;;) {
        const char *comma = strchr(p, ',');
        const size_t n = comma ? static_cast<size_t>(comma - p) : strlen(p);
        const int hi = n > 3 ? hexDigit(p[0]) : -1, lo = n > 3 ? hexDigit(p[1]) : -1;
        if (policy.count == NBT_DIAG_RULES || hi < 0 || lo < 0 || p[2] != ':' ||
            !parseClasses(p + 3, n - 3, &policy.rules[policy.count].classes)) {
            return -1;
        }
        const uint8_t ecu = static_cast<uint8_t>(hi << 4 | lo);
        if (policy.count && ecu <= policy.rules[policy.count - 1].ecu) {
            return -1; // ascending, each once
        }
        policy.rules[policy.count++].ecu = ecu;
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    if (out) {
        *out = policy;
    }
    return 0;
}

unsigned nbt_diag_request_class(const uint8_t *uds, size_t size) {
    if (!uds || !size) {
        return 0;
    }
    switch (uds[0]) {
    case 0x22: // read data by identifier: one or more 16-bit identifiers
        return size >= 3 && (size - 1) % 2 == 0 ? NBT_DIAG_READ : 0;
    case 0x2C: // define (01 by identifier, 02 by memory address) or clear (03) a dynamic one
        return size >= 2 && (uds[1] == 0x01 || uds[1] == 0x02 || uds[1] == 0x03) ? NBT_DIAG_READ
                                                                                 : 0;
    case 0x3E: // tester present
        return size == 2 && (uds[1] & 0x7f) == 0 ? NBT_DIAG_READ : 0;
    case 0x19: // read fault codes
        return size >= 2 ? NBT_DIAG_CODES : 0;
    case 0x14: // clear fault codes: a 3-byte group
        return size == 4 ? NBT_DIAG_CLEAR : 0;
    case 0x10: // the default or the extended session, before a clear or a control
        return size == 2 && ((uds[1] & 0x7f) == 0x01 || (uds[1] & 0x7f) == 0x03) ? NBT_DIAG_SESSION
                                                                                 : 0;
    case 0x2E: // write data by identifier: the identifier and at least one byte
    case 0x2F: // input/output control by identifier: the identifier and its parameter
        return size >= 4 ? NBT_DIAG_CONTROL : 0;
    case 0x31: // routine control: start, stop or results of a routine
        return size >= 4 && (uds[1] & 0x7f) >= 0x01 && (uds[1] & 0x7f) <= 0x03 ? NBT_DIAG_CONTROL
                                                                               : 0;
    default:
        return 0;
    }
}

unsigned nbt_diag_policy_allows(const nbt_diag_policy *policy, unsigned ecu, const uint8_t *uds,
                                size_t size) {
    const unsigned kind = nbt_diag_request_class(uds, size);
    if (!policy || !kind) {
        return 0;
    }
    for (unsigned i = 0; i < policy->count && i < NBT_DIAG_RULES; ++i) {
        if (policy->rules[i].ecu == ecu) {
            const unsigned classes = policy->rules[i].classes;
            const unsigned needed =
                kind == NBT_DIAG_SESSION ? (NBT_DIAG_CLEAR | NBT_DIAG_CONTROL) : kind;
            return (classes & needed) ? kind | (classes & NBT_DIAG_DRIVE) : 0;
        }
    }
    return 0;
}
