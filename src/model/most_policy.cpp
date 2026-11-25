// SPDX-License-Identifier: MPL-2.0
#include "nbt/model/most_policy.hpp"

#include <errno.h>
#include <string.h>

namespace nbt {
namespace model {
namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parseField(const char *p, size_t n, unsigned digits, bool allowAny, int *out) {
    if (allowAny && n == 1 && p[0] == '*') {
        *out = NBT_MOST_ANY;
        return true;
    }
    if (n != digits) {
        return false;
    }
    int value = 0;
    for (unsigned i = 0; i < digits; ++i) {
        const int d = hexDigit(p[i]);
        if (d < 0) {
            return false;
        }
        value = value * 16 + d;
    }
    *out = value;
    return true;
}

bool parseRule(const char *p, size_t n, nbt_most_rule *rule) {
    const char *end = p + n;
    const char *body = NULL;
    if (n > 4 && !memcmp(p, "rx:", 3)) {
        rule->kind = NBT_MOST_RX;
        body = p + 3;
    } else if (n > 4 && !memcmp(p, "tx:", 3)) {
        rule->kind = NBT_MOST_TX;
        body = p + 3;
    } else if (n > 5 && !memcmp(p, "ctl:", 4)) {
        rule->kind = NBT_MOST_CTL;
        body = p + 4;
    } else if (n > 5 && !memcmp(p, "sub:", 4)) {
        rule->kind = NBT_MOST_SUB;
        body = p + 4;
    } else {
        return false;
    }
    const char *dot1 =
        static_cast<const char *>(memchr(body, '.', static_cast<size_t>(end - body)));
    // Any FBlock: every message for `rx`, every command for `tx` (bridge 1.9).
    if (!dot1 ||
        !parseField(body, static_cast<size_t>(dot1 - body), 2,
                    rule->kind == NBT_MOST_RX || rule->kind == NBT_MOST_TX, &rule->fblock)) {
        return false;
    }
    const char *dot2 =
        static_cast<const char *>(memchr(dot1 + 1, '.', static_cast<size_t>(end - dot1 - 1)));
    if (rule->kind == NBT_MOST_CTL) {
        rule->fkt = NBT_MOST_ANY;
        return !dot2 &&
               parseField(dot1 + 1, static_cast<size_t>(end - dot1 - 1), 2, false, &rule->inst);
    }
    if (!dot2 ||
        !parseField(dot1 + 1, static_cast<size_t>(dot2 - dot1 - 1), 2, rule->kind != NBT_MOST_SUB,
                    &rule->inst) ||
        !parseField(dot2 + 1, static_cast<size_t>(end - dot2 - 1), 3, rule->kind != NBT_MOST_SUB,
                    &rule->fkt)) {
        return false;
    }
    if (rule->fblock == NBT_MOST_ANY && (rule->inst != NBT_MOST_ANY || rule->fkt != NBT_MOST_ANY)) {
        return false;
    }
    // The reserved functions 0x000-0x002: never subscribed to; sent to only
    // when a `tx` rule names one (bridge 1.9).
    if (rule->kind == NBT_MOST_SUB && rule->fkt != NBT_MOST_ANY && rule->fkt <= 2) {
        return false;
    }
    if (rule->kind == NBT_MOST_SUB && rule->fblock == 1) {
        return false; /* NetBlock has a different notification interface. */
    }
    return true;
}

bool sameRule(const nbt_most_rule &a, const nbt_most_rule &b) {
    return a.kind == b.kind && a.fblock == b.fblock && a.inst == b.inst && a.fkt == b.fkt;
}

bool matchesKind(const nbt_most_policy &policy, nbt_most_rule_kind kind, unsigned fblock,
                 unsigned inst, unsigned fkt) {
    for (unsigned i = 0; i < policy.count && i < NBT_MOST_POLICY_RULES; ++i) {
        const nbt_most_rule &r = policy.rules[i];
        if (r.kind == kind &&
            (r.fblock == NBT_MOST_ANY || static_cast<unsigned>(r.fblock) == fblock) &&
            (r.inst == NBT_MOST_ANY || static_cast<unsigned>(r.inst) == inst) &&
            (r.fkt == NBT_MOST_ANY || static_cast<unsigned>(r.fkt) == fkt)) {
            return true;
        }
    }
    return false;
}

} // namespace

core::Result<MostPolicy> MostPolicy::parse(const char *text) {
    if (!text) {
        return core::Result<MostPolicy>::failure(
            core::Status::error(core::kInvalidArgument, "missing MOST policy"));
    }
    size_t length = 0;
    while (length <= NBT_MOST_POLICY_MAX && text[length]) {
        ++length;
    }
    if (!length || length > NBT_MOST_POLICY_MAX) {
        return core::Result<MostPolicy>::failure(
            core::Status::error(core::kInvalidArgument, "invalid MOST policy length"));
    }
    MostPolicy result;
    unsigned controlsCount = 0;
    unsigned subscriptionsCount = 0;
    for (const char *p = text;;) {
        const char *comma = strchr(p, ',');
        const size_t n = comma ? static_cast<size_t>(comma - p) : strlen(p);
        nbt_most_rule rule;
        if (result.value_.count >= NBT_MOST_POLICY_RULES || !parseRule(p, n, &rule)) {
            return core::Result<MostPolicy>::failure(
                core::Status::error(core::kInvalidArgument, "invalid MOST policy rule"));
        }
        for (unsigned i = 0; i < result.value_.count; ++i) {
            if (sameRule(result.value_.rules[i], rule)) {
                return core::Result<MostPolicy>::failure(
                    core::Status::error(core::kInvalidArgument, "duplicate MOST policy rule"));
            }
        }
        if (rule.kind == NBT_MOST_CTL && ++controlsCount > NBT_MOST_POLICY_CONTROLS) {
            return core::Result<MostPolicy>::failure(
                core::Status::error(core::kInvalidArgument, "too many MOST control rules"));
        }
        if (rule.kind == NBT_MOST_SUB && ++subscriptionsCount > NBT_MOST_POLICY_SUBSCRIPTIONS) {
            return core::Result<MostPolicy>::failure(
                core::Status::error(core::kInvalidArgument, "too many MOST subscriptions"));
        }
        result.value_.rules[result.value_.count++] = rule;
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    for (unsigned i = 0; i < result.value_.count; ++i) {
        const nbt_most_rule &rule = result.value_.rules[i];
        if (rule.kind == NBT_MOST_SUB &&
            !result.receives((unsigned)rule.fblock, (unsigned)rule.inst, (unsigned)rule.fkt)) {
            return core::Result<MostPolicy>::failure(
                core::Status::error(core::kInvalidArgument, "MOST subscription needs rx coverage"));
        }
    }
    return core::Result<MostPolicy>::success(result);
}

bool MostPolicy::receives(unsigned fblock, unsigned instance, unsigned function) const {
    return matchesKind(value_, NBT_MOST_RX, fblock, instance, function);
}

bool MostPolicy::sends(unsigned fblock, unsigned instance, unsigned function) const {
    if (function > 2) {
        return matchesKind(value_, NBT_MOST_TX, fblock, instance, function);
    }
    // A reserved function only when a rule names it: a wildcard never covers it.
    for (unsigned i = 0; i < value_.count && i < NBT_MOST_POLICY_RULES; ++i) {
        const nbt_most_rule &r = value_.rules[i];
        if (r.kind == NBT_MOST_TX && static_cast<unsigned>(r.fkt) == function &&
            (r.fblock == NBT_MOST_ANY || static_cast<unsigned>(r.fblock) == fblock) &&
            (r.inst == NBT_MOST_ANY || static_cast<unsigned>(r.inst) == instance)) {
            return true;
        }
    }
    return false;
}

size_t MostPolicy::controls(uint16_t keys[NBT_MOST_POLICY_CONTROLS]) const {
    if (!keys) {
        return 0;
    }
    size_t n = 0;
    for (unsigned i = 0;
         i < value_.count && i < NBT_MOST_POLICY_RULES && n < NBT_MOST_POLICY_CONTROLS; ++i) {
        if (value_.rules[i].kind == NBT_MOST_CTL) {
            keys[n++] = static_cast<uint16_t>((value_.rules[i].fblock << 8) | value_.rules[i].inst);
        }
    }
    return n;
}

} // namespace model
} // namespace nbt

int nbt_most_policy_parse(const char *text, nbt_most_policy *out) {
    nbt::core::Result<nbt::model::MostPolicy> parsed = nbt::model::MostPolicy::parse(text);
    if (!parsed.get()) {
        return -1;
    }
    if (out) {
        *out = parsed.value().legacy();
    }
    return 0;
}

int nbt_most_policy_receives(const nbt_most_policy *policy, unsigned fblock, unsigned inst,
                             unsigned fkt) {
    if (!policy) {
        return 0;
    }
    nbt::model::MostPolicy wrapper;
    wrapper.legacy() = *policy;
    return wrapper.receives(fblock, inst, fkt) ? 1 : 0;
}

int nbt_most_policy_sends(const nbt_most_policy *policy, unsigned fblock, unsigned inst,
                          unsigned fkt) {
    if (!policy) {
        return 0;
    }
    nbt::model::MostPolicy wrapper;
    wrapper.legacy() = *policy;
    return wrapper.sends(fblock, inst, fkt) ? 1 : 0;
}

size_t nbt_most_policy_controls(const nbt_most_policy *policy,
                                uint16_t keys[NBT_MOST_POLICY_CONTROLS]) {
    if (!policy) {
        return 0;
    }
    nbt::model::MostPolicy wrapper;
    wrapper.legacy() = *policy;
    return wrapper.controls(keys);
}
