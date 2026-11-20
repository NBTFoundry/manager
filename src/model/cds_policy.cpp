// SPDX-License-Identifier: MPL-2.0
#include "nbt/cds_policy.h"

#include <string.h>

int nbt_cds_name_valid(const char *name, size_t length) {
    if (!name || length < 3 || length > NBT_CDS_NAME_MAX || name[0] == '.' ||
        name[length - 1] == '.') {
        return 0;
    }
    bool dot = false;
    for (size_t i = 0; i < length; ++i) {
        const char c = name[i];
        if (c == '.') {
            if (name[i - 1] == '.') {
                return 0;
            }
            dot = true;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '_')) {
            return 0;
        }
    }
    return dot ? 1 : 0;
}

int nbt_cds_policy_parse(const char *text, nbt_cds_policy *out) {
    nbt_cds_policy policy;
    if (!text || !memchr(text, 0, NBT_CDS_POLICY_MAX + 1) || !*text) {
        return -1;
    }
    memset(&policy, 0, sizeof(policy));
    for (const char *p = text;;) {
        const char *comma = strchr(p, ',');
        const size_t n = comma ? static_cast<size_t>(comma - p) : strlen(p);
        if (policy.count == NBT_CDS_NAMES || !nbt_cds_name_valid(p, n)) {
            return -1;
        }
        for (unsigned i = 0; i < policy.count; ++i) {
            if (strlen(policy.names[i]) == n && !memcmp(policy.names[i], p, n)) {
                return -1;
            }
        }
        memcpy(policy.names[policy.count], p, n);
        policy.names[policy.count][n] = 0;
        ++policy.count;
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
