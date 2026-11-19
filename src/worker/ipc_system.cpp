// SPDX-License-Identifier: MPL-2.0
/* SETUP and IDRIVE RESTART: the GUI's two requests about the manager's own
 * installation (PROTOCOL.md). SETUP brings the stable launcher and the iDrive
 * plugin to the confirmed release; IDRIVE RESTART, sent only after the driver
 * chose it on the car, restarts iDrive so that it loads a replaced plugin. */
#include "nbt/autostart.h"
#include "nbt/capture/protocol.h"
#include "nbt/fs.h"
#include "nbt/log.h"
#include "nbt/manager_refresh.h"
#include "nbt/storage/capture_install.hpp"
#include "nbt/util.h"
#include "nbt/worker/ipc_dispatcher.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

char *autostartWord(char **cursor) {
    char *start = *cursor;
    if (!start || !*start || *start == ' ') {
        return NULL;
    }
    char *space = strchr(start, ' ');
    if (space) {
        *space = 0;
        *cursor = space + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

} // namespace

core::Status IpcDispatcher::handleAutostart(char *cursor) {
    nbt_autostart state;
    char output[256];
    char *app = autostartWord(&cursor);
    char *mode = autostartWord(&cursor);
    if (!app || cursor || !nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager") ||
        nbt_fs_exists(root_.legacy(), NBT_FS_PROFILE, app, NULL) != 1 ||
        (mode && strcmp(mode, "on") && strcmp(mode, "off"))) {
        return control_.respond("ERR invalid-command\n");
    }
    if (mode) {
        if ((worker_.state().updateLock.valid() && !strcmp(app, worker_.state().app)) ||
            nbt_autostart_set(root_.legacy(), app, !strcmp(mode, "on"))) {
            return control_.respond("ERR settings\n");
        }
        return control_.respond("OK\n");
    }
    if (nbt_autostart_load(root_.legacy(), app, &state)) {
        return control_.respond("ERR settings\n");
    }
    const int length =
        snprintf(output, sizeof(output), "AUTOSTART\t%s\t%s\n", state.enabled ? "on" : "off",
                 state.failures >= NBT_AUTOSTART_FAILURE_LIMIT ? "blocked" : "ready");
    return length < 0 || length >= static_cast<int>(sizeof(output))
               ? core::Status::error(ERANGE, "autostart response too large")
               : control_.respond(output);
}

core::Status IpcDispatcher::handleSystem(bool setup, const char *cursor) {
    if (!setup) {
        if (!cursor || strcmp(cursor, "RESTART")) {
            return control_.respond("ERR invalid-command\n");
        }
        return control_.respond(nbt_system_restart_idrive() ? "ERR restart\n" : "OK\n");
    }
    if (cursor) {
        return control_.respond("ERR invalid-command\n");
    }
    if (worker_.state().updateLock.valid()) {
        return control_.respond("SETUP\tbusy\n");
    }
    const char *hmiRoot = "/opt/hmi/ID5/data/ro";
#ifdef NBT_TESTING
    hmiRoot = getenv("NBT_TEST_HMI_ROOT");
    if (!hmiRoot) {
        return control_.respond("ERR setup\n");
    }
#endif
    // A plugin replacement copies and syncs about 1 MB: on the worker's job,
    // the loop keeps answering meanwhile and the GUI's next commands wait.
    if (!worker_.startSetup(hmiRoot)) {
        return control_.respond("SETUP\tbusy\n");
    }
    setupPending_ = true;
    return core::Status();
}

core::Status IpcDispatcher::finishSetup() {
    int result = 0;
    unsigned changed = 0;
    char error[160];
    if (!worker_.takeSetup(&result, &changed, error, sizeof(error))) {
        return core::Status();
    }
    setupPending_ = false;
    if (result < 0) {
        nbt_logf("setup: %s", error);
        return control_.respond("ERR setup\n");
    }
    if (result > 0) {
        return control_.respond("SETUP\ttrial\n");
    }
    static const char *const kReplies[] = {"SETUP\tcurrent\n", "SETUP\tlauncher\n",
                                           "SETUP\tplugin\n", "SETUP\tboth\n"};
    const unsigned replyBits =
        (changed & NBT_REFRESH_LAUNCHER) |
        ((changed & (NBT_REFRESH_PLUGIN | NBT_REFRESH_CAPTURE)) ? NBT_REFRESH_PLUGIN : 0u);
    if (changed & NBT_REFRESH_CAPTURE_REFUSED) {
        nbt_logf("setup: %s", error); // the shim's tests refused it; the rest stands
    }
    if (replyBits) {
        nbt_logf("setup: %s replaced", replyBits == 3   ? "launcher and plugin"
                                       : replyBits == 1 ? "launcher"
                                                        : "plugin");
        if (changed & NBT_REFRESH_CAPTURE) {
            char capStatus[64];
            const char *diagPath = NBT_CAPTURE_DIAG_PATH;
#ifdef NBT_TESTING
            const char *overridePath = getenv("NBT_CAPTURE_DIAG_SHM");
            if (overridePath && *overridePath) {
                diagPath = overridePath;
            }
#endif
            (void)nbt_capture_operational_status(diagPath, capStatus, sizeof(capStatus));
            nbt_logf("setup: capture shim updated (%s; iDrive restart required)", capStatus);
        }
    }
    return control_.respond(kReplies[replyBits & 3u]);
}

} // namespace worker
} // namespace nbt
