// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/phone_companion.hpp"

#include "nbt/bridge.h"
#include "nbt/companion_wire.h"
#include "nbt/fs.h"
#include "nbt/process/companion_files.hpp"
#include "nbt/transaction.h"
#include "nbt/util.h"
#include "nbt/worker/worker.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

/* 200 when `app` runs the process that has the page (its service when it
 * declares one), 409 when it does not, 404 not installed or no `companion`,
 * 503 unknown. */
int pageOwner(Worker &worker, const char *app) {
    nbt_installed installed;
    if (!nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager")) {
        return 400;
    }
    if (nbt_tx_installed(worker.root().legacy(), app, &installed)) {
        return 404;
    }
    const nbt_bridge_requirement &bridge = installed.bridge;
    if (!nbt_bridge_cap_has(bridge.uses, "companion") &&
        !nbt_bridge_cap_has(bridge.optional, "companion")) {
        return 404;
    }
    const bool service = nbt_bridge_cap_has(bridge.uses, "service") ||
                         nbt_bridge_cap_has(bridge.optional, "service");
    core::Result<bool> running = service ? worker.serviceRunning(app) : worker.running(app);
    return !running.ok() ? 503 : running.value() ? 200 : 409;
}

const char *bridgeDir(Worker &worker) {
    return nbt_fs_bridge_dir(worker.root().legacy());
}

} // namespace

int phoneCompanion(Worker &worker, const char *body, core::Span<char> out) {
    char app[NBT_ID_MAX + 2];
    const size_t length = body ? strlen(body) : 0;
    if (length < 2 || length > NBT_ID_MAX + 1 || body[length - 1] != '\n') {
        return 400;
    }
    memcpy(app, body, length - 1);
    app[length - 1] = 0;
    const int owner = pageOwner(worker, app);
    static const char kReady[] = "COMPANION\tready\n";
    const size_t header = sizeof(kReady) - 1;
    int page = 0;
    if (owner == 200 && out.size() > header + NBT_COMPANION_PAGE_MAX) {
        page = process::companionPageRead(bridgeDir(worker), app, out.data() + header,
                                          out.size() - header);
    }
    if (owner != 200 && owner != 409) {
        return owner;
    }
    if (page < 0) {
        return 503;
    }
    if (!page) {
        snprintf(out.data(), out.size(), "COMPANION\toff\n");
        return 200;
    }
    memcpy(out.data(), kReady, header);
    out.data()[header + static_cast<size_t>(page)] = 0;
    return 200;
}

int phoneCompanionEvent(Worker &worker, const char *body, core::Span<char> out) {
    char text[NBT_ID_MAX + NBT_COMPANION_EVENT_LINE_MAX + 4];
    const size_t length = body ? strlen(body) : 0;
    if (length < 2 || length >= sizeof(text) || body[length - 1] != '\n') {
        return 400;
    }
    memcpy(text, body, length - 1);
    text[length - 1] = 0;
    char *id = strchr(text, '\t');
    char *value = id ? strchr(id + 1, '\t') : NULL;
    if (!value) {
        return 400;
    }
    *id++ = 0;
    *value++ = 0;
    if (!nbt_companion_id_valid(id, strlen(id)) ||
        !nbt_companion_value_valid(value, strlen(value))) {
        return 400;
    }
    const int owner = pageOwner(worker, text);
    if (owner != 200) {
        return owner;
    }
    if (process::companionEventAdd(bridgeDir(worker), text, id, value)) {
        return 503;
    }
    snprintf(out.data(), out.size(), "OK\n");
    return 200;
}

} // namespace worker
} // namespace nbt
