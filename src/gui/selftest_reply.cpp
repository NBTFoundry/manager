// SPDX-License-Identifier: MPL-2.0
/* The worker's SELFTEST replies (docs/self-test.md), parsed with bounds:
 * anything malformed is refused whole. */
#include "nbt/gui/selftest_ui.hpp"

#include "nbt/gui/ui_controller.hpp"
#include "nbt/util.h"

#include <string.h>

namespace nbt {
namespace gui {
namespace {

typedef SelfTestState S;

bool printable(const char *s, size_t max) {
    const size_t n = strlen(s);
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < 0x20 || s[i] > 0x7e) {
            return false;
        }
    }
    return n <= max;
}

} // namespace

bool selfTestParseReady(SelfTestState &t, const char *reply) {
    static const char kHead[] = "SELFTEST\tready\t";
    if (strncmp(reply, kHead, sizeof(kHead) - 1)) {
        return false;
    }
    t.count = t.next = 0;
    for (const char *p = reply + sizeof(kHead) - 1; *p;) {
        const size_t n = strcspn(p, ",");
        if (!n || n > kIdMax || t.count == NBT_BUNDLED_COUNT) {
            return false;
        }
        memcpy(t.apps[t.count], p, n);
        t.apps[t.count][n] = 0;
        if (!nbt_app_bundled(t.apps[t.count++])) {
            return false;
        }
        p += n + (p[n] ? 1 : 0);
    }
    return true;
}

/* "SELFTEST\t<passed>\t<failed>\t<skipped>", then "\t<group>.<check>|<f|s>|<detail>". */
bool selfTestParseReport(SelfTestState &t, const char *reply) {
    char copy[kReplyMax];
    char *fields[4 + S::kRows];
    if (strncmp(reply, "SELFTEST\t", 9) || strlen(reply) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, reply);
    size_t count = 0;
    char *save = NULL;
    for (char *f = strtok_r(copy, "\t", &save); f; f = strtok_r(NULL, "\t", &save)) {
        if (count == sizeof(fields) / sizeof(fields[0])) {
            return false;
        }
        fields[count++] = f;
    }
    uint64_t numbers[3];
    if (count < 4) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (nbt_parse_u64(fields[i + 1], &numbers[i], 0) || numbers[i] > 999) {
            return false;
        }
    }
    t.rows = 0;
    for (size_t i = 4; i < count; ++i) {
        char *key = fields[i], *code = strchr(key, '|');
        char *detail = code ? strchr(code + 1, '|') : NULL;
        if (!detail || code - key >= static_cast<long>(sizeof(t.keys[0])) || detail != code + 2 ||
            (code[1] != 'f' && code[1] != 's')) {
            return false;
        }
        *code = 0;
        if (!strchr(key, '.') || !printable(key, sizeof(t.keys[0]) - 1) ||
            !printable(detail + 1, sizeof(t.details[0]) - 1)) {
            return false;
        }
        strcpy(t.keys[t.rows], key);
        t.codes[t.rows] = code[1];
        strcpy(t.details[t.rows++], detail + 1);
    }
    t.passed = static_cast<unsigned>(numbers[0]);
    t.failed = static_cast<unsigned>(numbers[1]);
    t.skipped = static_cast<unsigned>(numbers[2]);
    t.have = true;
    return true;
}

} // namespace gui
} // namespace nbt
