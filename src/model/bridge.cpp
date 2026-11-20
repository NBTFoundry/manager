// SPDX-License-Identifier: MPL-2.0
#include "nbt/model/bridge.hpp"

#include "nbt/cds_policy.h"
#include "nbt/files_policy.h"
#include "nbt/most_policy.h"
#include "nbt/net_policy.h"
#include "nbt/util.h"

#include <errno.h>
#include <stddef.h>
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

bool declared(const nbt_bridge_requirement *v, const char *name) {
    return nbt_bridge_cap_has(v->uses, name) || nbt_bridge_cap_has(v->optional, name);
}

bool validRequirement(const nbt_bridge_requirement *v) {
    char name[NBT_BRIDGE_NAME_MAX + 1];
    if (!v || !memchr(v->uses, 0, sizeof(v->uses)) ||
        !memchr(v->optional, 0, sizeof(v->optional)) || !nbt_bridge_caps_valid(v->uses) ||
        !nbt_bridge_caps_valid(v->optional) || !memchr(v->most, 0, sizeof(v->most)) ||
        !memchr(v->files, 0, sizeof(v->files)) || !memchr(v->cds, 0, sizeof(v->cds)) ||
        !memchr(v->net, 0, sizeof(v->net))) {
        return false;
    }
    if (!v->major) {
        return !v->minor && !v->uses[0] && !v->optional[0] && !v->most[0] && !v->files[0] &&
               !v->cds[0] && !v->net[0];
    }
    for (const char *p = v->optional; *p;) {
        p = nextCapName(p, name);
        if (nbt_bridge_cap_has(v->uses, name)) {
            return false;
        }
    }
    if (nbt_bridge_cap_has(v->uses, "most") || nbt_bridge_cap_has(v->optional, "most")) {
        if (!v->most[0] || nbt_most_policy_parse(v->most, NULL) ||
            (v->major == 1 && v->minor < 2 && strstr(v->most, "sub:"))) {
            return false;
        }
    } else if (v->most[0]) {
        return false;
    }
    // `files` came with bridge 1.5: an older manager would not know its line.
    if (declared(v, "files")) {
        if (!v->files[0] || nbt_files_policy_parse(v->files, NULL) ||
            (v->major == 1 && v->minor < 5)) {
            return false;
        }
    } else if (v->files[0]) {
        return false;
    }
    // `car-data` and `net` came with bridge 1.6, likewise.
    if (declared(v, "car-data")) {
        if (!v->cds[0] || nbt_cds_policy_parse(v->cds, NULL) || (v->major == 1 && v->minor < 6)) {
            return false;
        }
    } else if (v->cds[0]) {
        return false;
    }
    if (declared(v, "net")) {
        return v->net[0] && !nbt_net_policy_parse(v->net, NULL) && !(v->major == 1 && v->minor < 6);
    }
    return !v->net[0];
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
    n = snprintf(field, sizeof(field), "%sbridge-files=", prefix);
    if (n < 0 || n >= static_cast<int>(sizeof(field))) {
        return -1;
    }
    if (!strncmp(*text, field, static_cast<size_t>(n))) {
        field[n - 1] = 0;
        if (!(v = nbt_field(text, field)) || strlen(v) > NBT_BRIDGE_FILES_MAX) {
            return -1;
        }
        memcpy(out->files, v, strlen(v) + 1);
    }
    static const struct {
        const char *name;
        size_t field, max;
    } kLater[] = {{"bridge-cds=", offsetof(nbt_bridge_requirement, cds), NBT_BRIDGE_CDS_MAX},
                  {"bridge-net=", offsetof(nbt_bridge_requirement, net), NBT_BRIDGE_NET_MAX}};
    for (size_t i = 0; i < sizeof(kLater) / sizeof(kLater[0]); ++i) {
        n = snprintf(field, sizeof(field), "%s%s", prefix, kLater[i].name);
        if (n < 0 || n >= static_cast<int>(sizeof(field))) {
            return -1;
        }
        if (!strncmp(*text, field, static_cast<size_t>(n))) {
            field[n - 1] = 0;
            if (!(v = nbt_field(text, field)) || strlen(v) > kLater[i].max) {
                return -1;
            }
            memcpy(reinterpret_cast<char *>(out) + kLater[i].field, v, strlen(v) + 1);
        }
    }
    return nbt::model::validRequirement(out) ? 0 : -1;
}

int nbt_bridge_requirement_valid(const nbt_bridge_requirement *v) {
    return nbt::model::validRequirement(v) ? 1 : 0;
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
        out, size,
        "%sbridge=%u.%u\n%sbridge-uses=%s\n%sbridge-optional=%s\n%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
        prefix, v->major, v->minor, prefix, v->uses, prefix, v->optional, v->most[0] ? prefix : "",
        v->most[0] ? "bridge-most=" : "", v->most, v->most[0] ? "\n" : "",
        v->files[0] ? prefix : "", v->files[0] ? "bridge-files=" : "", v->files,
        v->files[0] ? "\n" : "", v->cds[0] ? prefix : "", v->cds[0] ? "bridge-cds=" : "", v->cds,
        v->cds[0] ? "\n" : "", v->net[0] ? prefix : "", v->net[0] ? "bridge-net=" : "", v->net,
        v->net[0] ? "\n" : "");
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : n;
}
