// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/host_start.hpp"

#include "nbt/bridge.h"
#include "nbt/process/bridge_environment.hpp"
#include "nbt/process/host_control.hpp"
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

} // namespace

int prepareHostStart(nbt_fs *fs, const char *app, int lease, HostStart *out, char *reason,
                     size_t size, int *error, HostChannels &channels) {
    nbt_bridge_provider provider;
    nbt_bridge_version grant;
    char available[NBT_BRIDGE_CAPS_MAX + 1];
    char detail[128];
    out->canShow = out->canStatus = false;
    detail[0] = 0;
    errno = 0;
    int result = nbt_tx_recover(fs, app, detail, sizeof(detail));
    const int recoverError = errno;
    close(lease);
    if (result) {
        return refuse(reason, size, error, recoverError, "recovery", detail);
    }
    nbt_bridge_current(&provider);
    errno = 0;
    if (nbt_tx_installed(fs, app, &out->record)) {
        return refuse(reason, size, error, errno, "installed record unreadable", NULL);
    }
    channels.prepare(app, out->record.bridge, provider.versions[0].caps, available);
    // Files live on the hard disk: without a usable root the capability is
    // unavailable (a required one then refuses the start below).
    char folder[NBT_PATH_MAX];
    if (out->record.bridge.files[0] && nbt_bridge_cap_has(available, "files") &&
        !filesDir(app, folder, sizeof(folder))) {
        char filtered[NBT_BRIDGE_CAPS_MAX + 1];
        nbt_bridge_caps_without(available, "files", filtered);
        memcpy(available, filtered, sizeof(filtered));
    }
    // The grant first: the environment needs it too, and only this says what is missing.
    detail[0] = 0;
    result =
        nbt_bridge_check(&out->record.bridge, &provider, available, &grant, detail, sizeof(detail));
    if (result) {
        channels.releaseChildren();
        return refuse(reason, size, error, 0, "bridge grant refused", detail);
    }
    errno = 0;
    if (nbt_app_environment_build(fs, app, "user", &out->record.bridge, available,
                                  &out->environment) ||
        nbt_monotonic_ms(&out->nowMs)) {
        const int saved = errno;
        channels.releaseChildren();
        return refuse(reason, size, error, saved, "environment", NULL);
    }
    out->canShow = nbt_bridge_cap_has(grant.caps, "show");
    out->canStatus = nbt_bridge_cap_has(grant.caps, "status");
    return 0;
}

} // namespace process
} // namespace nbt
