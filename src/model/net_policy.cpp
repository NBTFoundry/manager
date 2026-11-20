// SPDX-License-Identifier: MPL-2.0
#include "nbt/net_policy.h"

#include <string.h>

namespace {

/* One rule, `n` bytes at `p`: tcp:<port>@<iface>[+<iface>]. */
bool parseRule(const char *p, size_t n, nbt_net_rule *rule) {
    if (n < 4 || memcmp(p, "tcp:", 4)) {
        return false;
    }
    size_t i = 4;
    unsigned port = 0, digits = 0;
    while (i < n && p[i] >= '0' && p[i] <= '9') {
        port = port * 10u + static_cast<unsigned>(p[i] - '0');
        if (++digits > 5 || port > 65535u) {
            return false;
        }
        ++i;
    }
    if (!digits || !port || (digits > 1 && p[4] == '0') || i >= n || p[i++] != '@') {
        return false;
    }
    unsigned interfaces = 0;
    for (;;) {
        unsigned bit;
        if (n - i >= 4 && !memcmp(p + i, "uap0", 4)) {
            bit = NBT_NET_UAP0;
        } else if (n - i >= 4 && !memcmp(p + i, "wfd0", 4)) {
            bit = NBT_NET_WFD0;
        } else {
            return false;
        }
        if (interfaces & bit) {
            return false;
        }
        interfaces |= bit;
        i += 4;
        if (i == n) {
            break;
        }
        if (p[i++] != '+') {
            return false;
        }
    }
    rule->port = static_cast<uint16_t>(port);
    rule->interfaces = interfaces;
    return true;
}

} // namespace

int nbt_net_policy_parse(const char *text, nbt_net_policy *out) {
    nbt_net_policy policy;
    if (!text || !memchr(text, 0, NBT_NET_POLICY_MAX + 1) || !*text) {
        return -1;
    }
    memset(&policy, 0, sizeof(policy));
    for (const char *p = text;;) {
        const char *comma = strchr(p, ',');
        const size_t n = comma ? static_cast<size_t>(comma - p) : strlen(p);
        nbt_net_rule rule;
        if (policy.count == NBT_NET_RULES || !parseRule(p, n, &rule)) {
            return -1;
        }
        for (unsigned i = 0; i < policy.count; ++i) {
            if (policy.rules[i].port == rule.port) {
                return -1;
            }
        }
        policy.rules[policy.count++] = rule;
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

const char *nbt_net_interface_name(unsigned bit) {
    return bit == NBT_NET_UAP0 ? "uap0" : bit == NBT_NET_WFD0 ? "wfd0" : NULL;
}
