// SPDX-License-Identifier: MPL-2.0
#include "nbt/model/bridge.hpp"

#include "nbt/core/text.hpp"
#include "nbt/most_policy.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace model {
namespace {

const char *nextCapName(const char *p, char name[NBT_BRIDGE_NAME_MAX + 1]) {
    const char *end = strchr(p, ',');
    const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
    memcpy(name, p, n);
    name[n] = 0;
    return end ? end + 1 : p + n;
}

bool validRequirement(const nbt_bridge_requirement *v) {
    return nbt_bridge_requirement_valid(v) != 0;
}

int failCheck(char *error, unsigned size, const char *message) {
    if (error && size) {
        snprintf(error, size, "%s", message);
    }
    return -1;
}

} // namespace

Provider Provider::current() {
    Provider provider;
    nbt_bridge_current(&provider.value_);
    return provider;
}

core::Status Provider::add(core::StringRef declaration) {
    char copy[NBT_BRIDGE_CAPS_MAX + 32];
    if (!core::copyTerminated(declaration, core::Span<char>(copy)).ok() ||
        nbt_bridge_provider_add(&value_, copy)) {
        return core::Status::error(core::kInvalidArgument, "invalid bridge provider declaration");
    }
    return core::Status();
}

core::Result<nbt_bridge_version> Provider::check(const BridgeRequirement &need,
                                                 const char *available,
                                                 core::Span<char> error) const {
    nbt_bridge_version grant;
    if (!error.data() || error.size() > static_cast<size_t>(UINT_MAX)) {
        return core::Result<nbt_bridge_version>::failure(
            core::Status::error(core::kInvalidArgument, "invalid error buffer"));
    }
    if (nbt_bridge_check(&need.legacy(), &value_, available, &grant, error.data(),
                         static_cast<unsigned>(error.size()))) {
        return core::Result<nbt_bridge_version>::failure(
            core::Status::error(core::kInvalidArgument, "bridge requirement not satisfied"));
    }
    return core::Result<nbt_bridge_version>::success(grant);
}

core::Result<uint64_t> parseStatus(core::Span<const char> line, core::Span<char> text) {
    uint64_t sequence = 0;
    if (text.size() < NBT_BRIDGE_STATUS_MAX + 1 ||
        nbt_bridge_status_parse(line.data(), line.size(), &sequence, text.data())) {
        return core::Result<uint64_t>::failure(
            core::Status::error(core::kInvalidArgument, "invalid bridge status"));
    }
    return core::Result<uint64_t>::success(sequence);
}

core::Result<size_t> formatStatus(uint64_t sequence, core::StringRef text,
                                  core::Span<char> output) {
    char copy[NBT_BRIDGE_STATUS_MAX + 1];
    if (!core::copyTerminated(text, core::Span<char>(copy)).ok()) {
        return core::Result<size_t>::failure(
            core::Status::error(core::kInvalidArgument, "invalid bridge status text"));
    }
    const int result = nbt_bridge_status_format(sequence, copy, output.data(), output.size());
    if (result < 0) {
        return core::Result<size_t>::failure(
            core::Status::error(core::kInvalidArgument, "invalid bridge status or output size"));
    }
    return core::Result<size_t>::success(static_cast<size_t>(result));
}

} // namespace model
} // namespace nbt

int nbt_bridge_provider_add(nbt_bridge_provider *out, const char *text) {
    char version[12];
    if (!out || !text || out->count >= NBT_BRIDGE_VERSIONS_MAX) {
        return -1;
    }
    const char *sep = strchr(text, '|');
    if (!sep) {
        return -1;
    }
    const size_t n = static_cast<size_t>(sep - text);
    if (n >= sizeof(version) || !nbt_bridge_caps_valid(sep + 1)) {
        return -1;
    }
    memcpy(version, text, n);
    version[n] = 0;
    nbt_bridge_version value;
    memset(&value, 0, sizeof(value));
    if (nbt_bridge_version_parse(version, &value.major, &value.minor) ||
        (out->count && out->versions[out->count - 1].major >= value.major)) {
        return -1;
    }
    memcpy(value.caps, sep + 1, strlen(sep + 1) + 1);
    out->versions[out->count++] = value;
    return 0;
}

void nbt_bridge_current(nbt_bridge_provider *out) {
    memset(out, 0, sizeof(*out));
    out->count = 1;
    out->versions[0].major = 1;
    out->versions[0].minor = 8;
    out->versions[0].oldest = NBT_BRIDGE_OLDEST_MINOR;
    static const char kCaps[] =
        "show,status,live-settings,most,gamepad,controller,files,hold,car-data,net,runner,diag";
    memcpy(out->versions[0].caps, kCaps, sizeof(kCaps));
}

int nbt_bridge_check(const nbt_bridge_requirement *need, const nbt_bridge_provider *provider,
                     const char *available, nbt_bridge_version *grant, char *error, unsigned size) {
    char name[NBT_BRIDGE_NAME_MAX + 1];
    nbt_bridge_version value;
    if (error && size) {
        *error = 0;
    }
    memset(&value, 0, sizeof(value));
    if (!nbt::model::validRequirement(need) || !provider ||
        provider->count > NBT_BRIDGE_VERSIONS_MAX ||
        (available && !nbt_bridge_caps_valid(available))) {
        return nbt::model::failCheck(error, size, "invalid bridge declaration");
    }
    if (!need->major) {
        if (grant) {
            *grant = value;
        }
        return 0;
    }
    const nbt_bridge_version *served = NULL;
    for (unsigned i = 0; i < provider->count; ++i) {
        if (provider->versions[i].major == need->major) {
            served = &provider->versions[i];
        }
    }
    if (served && need->minor < served->oldest) {
        if (error && size) {
            snprintf(error, size, "declares bridge %u.%u; this manager runs %u.%u and later",
                     need->major, need->minor, served->major, served->oldest);
        }
        return -1;
    }
    if (!served || served->minor < need->minor) {
        if (error && size) {
            if (served) {
                snprintf(error, size, "needs bridge %u.%u; this manager has %u.%u", need->major,
                         need->minor, served->major, served->minor);
            } else {
                snprintf(error, size, "needs bridge %u.%u; this manager does not serve that major",
                         need->major, need->minor);
            }
        }
        return -1;
    }
    if (!memchr(served->caps, 0, sizeof(served->caps)) || !nbt_bridge_caps_valid(served->caps)) {
        return nbt::model::failCheck(error, size, "invalid bridge declaration");
    }
    if (need->most[0] && !nbt_bridge_cap_has(served->caps, "most")) {
        return nbt::model::failCheck(error, size,
                                     "declares MOST functions; this manager does not serve most");
    }
    if (need->files[0] && !nbt_bridge_cap_has(served->caps, "files")) {
        return nbt::model::failCheck(error, size,
                                     "declares files; this manager does not serve files");
    }
    if (need->cds[0] && !nbt_bridge_cap_has(served->caps, "car-data")) {
        return nbt::model::failCheck(error, size,
                                     "declares car data; this manager does not serve car-data");
    }
    if (need->net[0] && !nbt_bridge_cap_has(served->caps, "net")) {
        return nbt::model::failCheck(error, size,
                                     "declares network rules; this manager does not serve net");
    }
    for (const char *p = need->uses; *p;) {
        p = nbt::model::nextCapName(p, name);
        if (!nbt_bridge_cap_has(served->caps, name) ||
            (available && !nbt_bridge_cap_has(available, name))) {
            if (error && size) {
                snprintf(error, size, "needs %s; %s", name,
                         !nbt_bridge_cap_has(served->caps, name) ? "not implemented by this manager"
                                                                 : "not available on this car");
            }
            return -1;
        }
    }
    value.major = served->major;
    value.minor = served->minor;
    size_t used = 0;
    for (unsigned i = 0; i < 2; ++i) {
        for (const char *p = i ? need->optional : need->uses; *p;) {
            p = nbt::model::nextCapName(p, name);
            if (!nbt_bridge_cap_has(served->caps, name) ||
                (available && !nbt_bridge_cap_has(available, name))) {
                continue;
            }
            const size_t n = strlen(name);
            if (used + (used != 0) + n > NBT_BRIDGE_CAPS_MAX) {
                return nbt::model::failCheck(error, size, "invalid bridge declaration");
            }
            if (used) {
                value.caps[used++] = ',';
            }
            memcpy(value.caps + used, name, n + 1);
            used += n;
        }
    }
    if (grant) {
        *grant = value;
    }
    return 0;
}
