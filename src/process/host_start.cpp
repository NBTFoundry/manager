// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/host_start.hpp"

#include "nbt/bridge.h"
#include "nbt/process/bridge_environment.hpp"
#include "nbt/process/host_control.hpp"
#include "nbt/process/runner_path.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

int refuse(char *reason, size_t size, int *error, int code, const char *what, const char *detail) {
    if (detail && detail[0]) {
        snprintf(reason, size, "cannot start: %s: %s", what, detail);
    } else {
        snprintf(reason, size, "cannot start: %s", what);
    }
    *error = code;
    return -1;
}

// What a service never gets: the screen and the input (docs/service-bridge.md).
const char *const kScreenCaps[] = {"show", "hold", "controller", "gamepad", "status"};

void without(char *caps, const char *name) {
    char rest[NBT_BRIDGE_CAPS_MAX + 1];
    nbt_bridge_caps_without(caps, name, rest);
    memcpy(caps, rest, sizeof(rest));
}

/* `name` becomes optional (still declared) and is not offered. */
void notOffered(nbt_bridge_requirement *need, char *available, const char *name) {
    if (nbt_bridge_cap_has(need->uses, name)) {
        without(need->uses, name);
        const size_t used = strlen(need->optional);
        snprintf(need->optional + used, sizeof(need->optional) - used, "%s%s", used ? "," : "",
                 name);
    }
    without(available, name);
}

} // namespace

int prepareHostStart(nbt_fs *fs, const char *app, int lease, HostStart *out, char *reason,
                     size_t size, int *error, HostChannels &channels, bool service) {
    nbt_bridge_provider provider;
    nbt_bridge_version grant;
    char available[NBT_BRIDGE_CAPS_MAX + 1];
    char detail[128];
    out->canShow = out->canStatus = false;
    out->grantCaps[0] = 0;
    detail[0] = 0;
    errno = 0;
    int result = lease >= 0 ? nbt_tx_recover(fs, app, detail, sizeof(detail)) : 0;
    const int recoverError = errno;
    if (lease >= 0) {
        close(lease);
    }
    if (result) {
        return refuse(reason, size, error, recoverError, "recovery", detail);
    }
    nbt_bridge_current(&provider);
    withoutMissingRunner(&provider);
    errno = 0;
    if (nbt_tx_installed(fs, app, &out->record)) {
        return refuse(reason, size, error, errno, "installed record unreadable", NULL);
    }
    nbt_bridge_requirement need = out->record.bridge;
    char served[NBT_BRIDGE_CAPS_MAX + 1];
    memcpy(served, provider.versions[0].caps, sizeof(served));
    if (service) {
        // The service never gets the screen or the input (`service` needs `show`).
        for (const char *name : kScreenCaps) {
            notOffered(&need, served, name);
        }
    } else if (nbt_bridge_cap_has(need.uses, "service") ||
               nbt_bridge_cap_has(need.optional, "service")) {
        // An application with a service leaves its phone page to it.
        notOffered(&need, served, "companion");
    }
    channels.prepare(app, need, served, available);
    // Files live on the hard disk: without a usable root the capability is
    // unavailable (a required one then refuses the start below).
    char folder[NBT_PATH_MAX];
    if (need.files[0] && nbt_bridge_cap_has(available, "files") &&
        !filesDir(app, folder, sizeof(folder))) {
        char filtered[NBT_BRIDGE_CAPS_MAX + 1];
        nbt_bridge_caps_without(available, "files", filtered);
        memcpy(available, filtered, sizeof(filtered));
    }
    // The grant first: the environment needs it too, and only this says what is missing.
    detail[0] = 0;
    result = nbt_bridge_check(&need, &provider, available, &grant, detail, sizeof(detail));
    if (!result && service && !nbt_bridge_cap_has(grant.caps, "service")) {
        result = -1;
        snprintf(detail, sizeof(detail), "it declares no service");
    }
    if (result) {
        channels.releaseChildren();
        return refuse(reason, size, error, 0, "bridge grant refused", detail);
    }
    errno = 0;
    if (nbt_app_environment_build(fs, app, "user", &need, available, &out->environment) ||
        nbt_monotonic_ms(&out->nowMs)) {
        const int saved = errno;
        channels.releaseChildren();
        return refuse(reason, size, error, saved, "environment", NULL);
    }
    out->canShow = nbt_bridge_cap_has(grant.caps, "show");
    out->canStatus = nbt_bridge_cap_has(grant.caps, "status");
    memcpy(out->grantCaps, grant.caps, sizeof(out->grantCaps));
    return 0;
}

} // namespace process
} // namespace nbt
