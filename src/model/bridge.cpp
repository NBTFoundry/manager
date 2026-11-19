#include "nbt/model/bridge.hpp"

#include "nbt/most_policy.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace model {
namespace {

bool parseVersionPart(const char *text, size_t n, uint16_t *out) {
    if (!n || n > 5 || (n > 1 && text[0] == '0')) {
        return false;
    }
    unsigned value = 0;
    for (size_t i = 0; i < n; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(text[i] - '0');
    }
    if (value > 65535) {
        return false;
    }
    *out = static_cast<uint16_t>(value);
    return true;
}

const char *nextCapName(const char *p, char name[NBT_BRIDGE_NAME_MAX + 1]) {
    const char *end = strchr(p, ',');
    const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
    memcpy(name, p, n);
    name[n] = 0;
    return end ? end + 1 : p + n;
}

bool validRequirement(const nbt_bridge_requirement *v) {
    char name[NBT_BRIDGE_NAME_MAX + 1];
    if (!v || !memchr(v->uses, 0, sizeof(v->uses)) ||
        !memchr(v->optional, 0, sizeof(v->optional)) || !nbt_bridge_caps_valid(v->uses) ||
        !nbt_bridge_caps_valid(v->optional) || !memchr(v->most, 0, sizeof(v->most))) {
        return false;
    }
    if (!v->major) {
        return !v->minor && !v->uses[0] && !v->optional[0] && !v->most[0];
    }
    for (const char *p = v->optional; *p;) {
        p = nextCapName(p, name);
        if (nbt_bridge_cap_has(v->uses, name)) {
            return false;
        }
    }
    if (nbt_bridge_cap_has(v->uses, "most") || nbt_bridge_cap_has(v->optional, "most")) {
        return v->most[0] != 0 && nbt_most_policy_parse(v->most, NULL) == 0 &&
               !(v->major == 1 && v->minor < 2 && strstr(v->most, "sub:"));
    }
    return !v->most[0];
}

} // namespace

core::Result<BridgeRequirement> BridgeRequirement::parse(char **text, const char *prefix) {
    nbt_bridge_requirement value;
    if (nbt_bridge_requirement_parse(text, prefix, &value)) {
        return core::Result<BridgeRequirement>::failure(
            core::Status::error(core::kInvalidArgument, "invalid bridge requirement"));
    }
    return core::Result<BridgeRequirement>::success(BridgeRequirement(value));
}

core::Result<size_t> BridgeRequirement::format(const char *prefix, core::Span<char> output) const {
    const int result = nbt_bridge_requirement_format(&value_, prefix, output.data(), output.size());
    if (result < 0) {
        return core::Result<size_t>::failure(core::Status::error(
            core::kInvalidArgument, "invalid bridge requirement or output size"));
    }
    return core::Result<size_t>::success(static_cast<size_t>(result));
}

} // namespace model
} // namespace nbt

int nbt_bridge_version_parse(const char *text, uint16_t *major, uint16_t *minor) {
    if (!text || !major || !minor) {
        return -1;
    }
    const char *dot = strchr(text, '.');
    uint16_t a = 0;
    uint16_t b = 0;
    if (!dot || !nbt::model::parseVersionPart(text, static_cast<size_t>(dot - text), &a) || !a ||
        !nbt::model::parseVersionPart(dot + 1, strlen(dot + 1), &b)) {
        return -1;
    }
    *major = a;
    *minor = b;
    return 0;
}

int nbt_bridge_cap_has(const char *list, const char *name) {
    const size_t n = strlen(name);
    const char *p = list;
    while (*p) {
        const char *end = strchr(p, ',');
        const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
        if (len == n && !memcmp(p, name, n)) {
            return 1;
        }
        p += len;
        if (*p) {
            ++p;
        }
    }
    return 0;
}

int nbt_bridge_caps_valid(const char *text) {
    if (!text || strlen(text) > NBT_BRIDGE_CAPS_MAX) {
        return 0;
    }
    const char *start = text;
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, ',');
        const char *old = start;
        const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
        if (!n || n > NBT_BRIDGE_NAME_MAX || *p < 'a' || *p > 'z') {
            return 0;
        }
        for (size_t i = 0; i < n; ++i) {
            const char c = p[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
                return 0;
            }
        }
        while (old < p) {
            const char *comma = strchr(old, ',');
            if (!comma || (static_cast<size_t>(comma - old) == n && !memcmp(old, p, n))) {
                return 0;
            }
            old = comma + 1;
        }
        if (!end) {
            return 1;
        }
        p = end + 1;
        if (!*p) {
            return 0;
        }
    }
    return 1;
}

int nbt_bridge_requirement_parse(char **text, const char *prefix, nbt_bridge_requirement *out) {
    char field[40];
    if (!text || !*text || !prefix || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    int n = snprintf(field, sizeof(field), "%sbridge=", prefix);
    if (n < 0 || n >= static_cast<int>(sizeof(field))) {
        return -1;
    }
    if (strncmp(*text, field, static_cast<size_t>(n))) {
        return 0;
    }
    field[n - 1] = 0;
    char *v = nbt_field(text, field);
    if (!v || nbt_bridge_version_parse(v, &out->major, &out->minor)) {
        return -1;
    }
    n = snprintf(field, sizeof(field), "%sbridge-uses", prefix);
    if (n < 0 || n >= static_cast<int>(sizeof(field)) || !(v = nbt_field(text, field)) ||
        !nbt_bridge_caps_valid(v)) {
        return -1;
    }
    memcpy(out->uses, v, strlen(v) + 1);
    n = snprintf(field, sizeof(field), "%sbridge-optional", prefix);
    if (n < 0 || n >= static_cast<int>(sizeof(field)) || !(v = nbt_field(text, field)) ||
        !nbt_bridge_caps_valid(v)) {
        return -1;
    }
    memcpy(out->optional, v, strlen(v) + 1);
    n = snprintf(field, sizeof(field), "%sbridge-most=", prefix);
    if (n < 0 || n >= static_cast<int>(sizeof(field))) {
        return -1;
    }
    if (!strncmp(*text, field, static_cast<size_t>(n))) {
        field[n - 1] = 0;
        if (!(v = nbt_field(text, field)) || strlen(v) > NBT_BRIDGE_MOST_MAX) {
            return -1;
        }
        memcpy(out->most, v, strlen(v) + 1);
    }
    return nbt::model::validRequirement(out) ? 0 : -1;
}

int nbt_bridge_requirement_format(const nbt_bridge_requirement *v, const char *prefix, char *out,
                                  size_t size) {
    if (!prefix || !out || !size || !nbt::model::validRequirement(v)) {
        return -1;
    }
    if (!v->major) {
        *out = 0;
        return 0;
    }
    const int n = snprintf(
        out, size, "%sbridge=%u.%u\n%sbridge-uses=%s\n%sbridge-optional=%s\n%s%s%s%s", prefix,
        v->major, v->minor, prefix, v->uses, prefix, v->optional, v->most[0] ? prefix : "",
        v->most[0] ? "bridge-most=" : "", v->most, v->most[0] ? "\n" : "");
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : n;
}
