// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/autostart_request.hpp"
#include "nbt/autostart.h"
#include "nbt/fs.h"
#include "nbt/util.h"
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {

AutostartResult autostartRequest(Worker &worker, const char *app, const char *mode,
                                 core::Span<char> out) {
    if (!app || !nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager") ||
        nbt_fs_exists(worker.root().legacy(), NBT_FS_PROFILE, app, NULL) != 1 ||
        (mode && strcmp(mode, "on") && strcmp(mode, "off"))) {
        return AutostartInvalid;
    }
    int length;
    if (mode) {
        if ((worker.state().updateLock.valid() && !strcmp(app, worker.state().app)) ||
            nbt_autostart_set(worker.root().legacy(), app, !strcmp(mode, "on"))) {
            return AutostartRefused;
        }
        length = snprintf(out.data(), out.size(), "OK\n");
    } else {
        nbt_autostart state;
        if (nbt_autostart_load(worker.root().legacy(), app, &state)) {
            return AutostartRefused;
        }
        length =
            snprintf(out.data(), out.size(), "AUTOSTART\t%s\t%s\n", state.enabled ? "on" : "off",
                     state.failures >= NBT_AUTOSTART_FAILURE_LIMIT ? "blocked" : "ready");
    }
    return length < 0 || static_cast<size_t>(length) >= out.size() ? AutostartRefused
                                                                   : AutostartDone;
}

} // namespace worker
} // namespace nbt
