// SPDX-License-Identifier: MPL-2.0
#include "nbt/permissions.h"

#include "nbt/diag_policy.h"
#include "nbt/most_policy.h"

#include <stdio.h>
#include <string.h>

namespace {

bool declares(const nbt_bridge_requirement *bridge, const char *cap) {
    return nbt_bridge_cap_has(bridge->uses, cap) || nbt_bridge_cap_has(bridge->optional, cap);
}

unsigned diagPermissions(const char *text) {
    nbt_diag_policy policy;
    unsigned classes = 0, out = 0;
    if (!text[0] || nbt_diag_policy_parse(text, &policy)) {
        return 0;
    }
    for (unsigned i = 0; i < policy.count; ++i) {
        classes |= policy.rules[i].classes;
    }
    out |= (classes & (NBT_DIAG_READ | NBT_DIAG_CODES)) ? NBT_PERM_ENGINE : 0u;
    out |= (classes & NBT_DIAG_CLEAR) ? NBT_PERM_CLEAR : 0u;
    out |= (classes & NBT_DIAG_CONTROL) ? NBT_PERM_CONTROL : 0u;
    return out | ((classes & NBT_DIAG_DRIVE) ? NBT_PERM_DRIVE : 0u);
}

unsigned mostPermissions(const char *text) {
    nbt_most_policy policy;
    unsigned out = 0;
    if (!text[0] || nbt_most_policy_parse(text, &policy)) {
        return 0;
    }
    for (unsigned i = 0; i < policy.count && i < NBT_MOST_POLICY_RULES; ++i) {
        const nbt_most_rule &r = policy.rules[i];
        if (r.kind == NBT_MOST_RX || r.kind == NBT_MOST_SUB) {
            out |= NBT_PERM_MOST_READ;
        } else if (r.kind == NBT_MOST_TX) {
            out |= NBT_PERM_MOST_SEND;
            out |= r.fblock == NBT_MOST_ANY || (r.fkt != NBT_MOST_ANY && r.fkt <= 2)
                       ? NBT_PERM_MOST_ANY
                       : 0u;
        }
    }
    return out;
}

} // namespace

unsigned nbt_permissions(const nbt_bridge_requirement *bridge) {
    if (!bridge || !bridge->major) {
        return 0;
    }
    unsigned out = declares(bridge, "service") ? NBT_PERM_BACKGROUND : 0u;
    out |= declares(bridge, "car-data") ? NBT_PERM_CAR_DATA : 0u;
    out |= declares(bridge, "files") ? NBT_PERM_FILES : 0u;
    out |= declares(bridge, "net") ? NBT_PERM_PHONE_NET : 0u;
    out |= declares(bridge, "diag") ? diagPermissions(bridge->diag) : 0u;
    return out | (declares(bridge, "most") ? mostPermissions(bridge->most) : 0u);
}

int nbt_permissions_format(unsigned granted, unsigned added, char *out, size_t size) {
    const int n = out ? snprintf(out, size, "perm:%x:%x", granted, added) : -1;
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : n;
}

int nbt_permissions_parse(const char *text, unsigned *granted, unsigned *added) {
    char end = 0;
    if (!text || !granted || !added || strncmp(text, "perm:", 5) ||
        sscanf(text, "perm:%x:%x%c", granted, added, &end) != 2 || (*granted & ~NBT_PERM_ALL) ||
        (*added & ~*granted)) {
        return -1;
    }
    return 0;
}
