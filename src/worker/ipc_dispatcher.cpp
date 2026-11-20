// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/ipc_dispatcher.hpp"

#include "nbt/app_host.h"
#include "nbt/app_settings.h"
#include "nbt/autostart.h"
#include "nbt/bridge.h"
#include "nbt/core/secure.hpp"
#include "nbt/core/text.hpp"
#include "nbt/fs.h"
#include "nbt/log.h"
#include "nbt/manager_settings.h"
#include "nbt/transaction.h"
#include "nbt/util.h"
#include "nbt/worker/wifi_direct.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

char *word(char **cursor) {
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

bool terminateLine(char *list, size_t capacity, size_t used) {
    if (used + 2 > capacity) {
        return false;
    }
    list[used] = '\n';
    list[used + 1] = 0;
    return true;
}

} // namespace

IpcDispatcher::IpcDispatcher(storage::ManagedRoot &root, Worker &worker, WebServer &web,
                             WifiDirectJob &wfd, ControlProtocol &control)
    : root_(root), worker_(worker), web_(web), wfd_(wfd), control_(control),
      files_(root, worker, web, control) {
}

core::Status IpcDispatcher::handleSettings(bool setting, char *cursor) {
    nbt_settings settings;
    nbt_autostart autostart = {0, 0};
    int mode = 0, live = 0, files = 0, update = 0; // update: disabled until updated (app_compat.h)
    char *app = word(&cursor);
    char *key = setting ? word(&cursor) : NULL;
    char *val = setting ? word(&cursor) : NULL;
    char *option = setting ? NULL : word(&cursor);
    if (option) {
        if (strcmp(option, "live")) {
            return control_.respond("ERR invalid-command\n");
        }
        mode = 1;
    }
    if (!app || cursor || (setting && (!key || !val)) || !nbt_token_valid(app, NBT_ID_MAX, 0)) {
        return control_.respond("ERR invalid-command\n");
    }
    const bool manager = !strcmp(app, "manager");
    if (!manager && nbt_fs_exists(root_.legacy(), NBT_FS_PROFILE, app, NULL) != 1) {
        return control_.respond("ERR invalid-command\n");
    }
    if (setting && manager) {
        if (nbt_manager_settings_set(root_.legacy(), key, val)) {
            return control_.respond("ERR settings\n");
        }
        if (!strcmp(key, "debug_log") && !strcmp(val, "on") &&
            nbt_log_open(root_.legacy(), "worker") > 0) {
            nbt_logf("debug log switched on");
        }
        return control_.respond("OK\n");
    }
    if (setting && worker_.state().updateLock.valid() && !strcmp(app, worker_.state().app)) {
        return control_.respond("ERR settings\n");
    }
    if (setting) {
        return control_.respond(
            nbt_app_settings_set(root_.legacy(), app, key, val) ? "ERR settings\n" : "OK\n");
    }
    if (manager ? nbt_manager_settings_load(root_.legacy(), &settings)
                : nbt_app_settings_load(root_.legacy(), app, &settings)) {
        return control_.respond("ERR settings\n");
    }
    if (mode && !manager) {
        nbt_installed installed;
        nbt_bridge_provider provider;
        nbt_bridge_version grant;
        nbt_bridge_current(&provider);
        if (nbt_tx_installed(root_.legacy(), app, &installed)) {
            return control_.respond("ERR settings\n");
        }
        update = nbt_bridge_check(&installed.bridge, &provider, provider.versions[0].caps, &grant,
                                  NULL, 0) != 0;
        live = !update && nbt_bridge_cap_has(grant.caps, "live-settings");
        files = !update && nbt_bridge_cap_has(grant.caps, "files"); // its page offers Files
        // Start with the car: shown as off when its record cannot be read.
        if (nbt_autostart_load(root_.legacy(), app, &autostart)) {
            autostart.enabled = 0;
        }
    }
    const bool paused = autostart.failures >= NBT_AUTOSTART_FAILURE_LIMIT;
    char list[2048];
    int length = snprintf(list, sizeof(list), "SETTINGS\t%u%s%s%s%s%s",
                          static_cast<unsigned>(settings.count), mode ? "\t" : "",
                          mode ? (live ? "live" : "restart") : "", files ? "+files" : "",
                          !autostart.enabled ? ""
                          : paused           ? "+autostart-paused"
                                             : "+autostart",
                          update ? "+update" : "");
    if (length < 0 || length >= static_cast<int>(sizeof(list))) {
        return core::Status::error(ERANGE, "settings too large");
    }
    size_t used = static_cast<size_t>(length);
    for (size_t i = 0; i < settings.count; ++i) {
        if (used + 2 >= sizeof(list)) {
            return core::Status::error(ERANGE, "settings too large");
        }
        list[used++] = '\t';
        length = nbt_setting_format(&settings.items[i], list + used, sizeof(list) - used);
        if (length < 0) {
            return core::Status::error(ERANGE, "settings too large");
        }
        used += static_cast<size_t>(length);
    }
    if (!terminateLine(list, sizeof(list), used)) {
        return core::Status::error(ERANGE, "settings too large");
    }
    return control_.respond(list);
}

core::Status IpcDispatcher::handleStart(char *cursor, uint64_t nowMs) {
    char *app = word(&cursor);
    if (!app || cursor) {
        return control_.respond("ERR invalid-command\n");
    }
    if (!nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager") ||
        nbt_fs_exists(root_.legacy(), NBT_FS_PROFILE, app, NULL) != 1) {
        return control_.respond("ERR start\n");
    }
    if ((worker_.state().updateLock.valid() && !strcmp(app, worker_.state().app)) ||
        nbt_autostart_clear(root_.legacy(), app)) {
        return control_.respond("ERR settings\n");
    }
    core::Result<StartResult> result = worker_.start(app, nowMs);
    if (!result.get()) {
        return control_.respond("ERR start\n");
    }
    // In StartResult's order.
    static const char *const kReplies[] = {
        "OK\n",         "ERR running\n",     "OK screen\n",
        "ERR screen\n", "ERR quarantined\n", "ERR incompatible\n"};
    return control_.respond(kReplies[static_cast<int>(result.value())]);
}

core::Status IpcDispatcher::handleSession(char *cursor, uint64_t nowMs) {
    char *app = word(&cursor);
    char *iface = word(&cursor);
    char *portText = word(&cursor);
    if (app && iface && !portText) {
        portText = iface;
        iface = app;
        app = NULL;
    }
    core::Result<uint64_t> parsedPort =
        core::parseU64(core::StringRef(portText ? portText : ""), false);
    if (!iface || !portText || cursor || !parsedPort.get() || parsedPort.value() > 65535 ||
        strlen(iface) >= sizeof(sessionIface_) || (app && strlen(app) >= sizeof(sessionApp_))
#ifndef NBT_TESTING
        || !parsedPort.value()
#endif
    ) {
        return control_.respond("ERR invalid-command\n");
    }
    web_.close(worker_);
    if (!worker_.openSession(app, nowMs).ok()) {
        return control_.respond("ERR session\n");
    }
    snprintf(sessionApp_, sizeof(sessionApp_), "%s", app ? app : "");
    snprintf(sessionIface_, sizeof(sessionIface_), "%s", iface);
    sessionPort_ = static_cast<unsigned>(parsedPort.value());
    if (wfd_.required(iface)) {
        if (!wfd_.start(iface, nowMs)) {
            ReadyWifiCredentials none;
            return finishSession(false, none, nowMs);
        }
        sessionPending_ = true; // answered by poll() once prepared, up to 10 s later
        return core::Status();
    }
    ReadyWifiCredentials wifi; // nothing to prepare on this interface
    return finishSession(true, wifi, nowMs);
}

core::Status IpcDispatcher::handleApprove(char *cursor, uint64_t nowMs) {
    core::SecureBytes<32> digest;
    char *hex = word(&cursor);
    char *mode = word(&cursor);
    if (!hex || !mode || cursor ||
        !core::hexDecode(core::StringRef(hex), core::Span<uint8_t>(digest.data(), digest.size()))
             .ok() ||
        (strcmp(mode, "normal") && strcmp(mode, "recovery"))) {
        return control_.respond("ERR invalid-command\n");
    }
    const core::Status approved = worker_.approve(digest.data(), !strcmp(mode, "recovery"), nowMs);
    return control_.respond(approved.ok() ? "OK installing\n" : "ERR install\n");
}

core::Status IpcDispatcher::dispatch(char *line, uint64_t nowMs) {
    if (control_.negotiated() && !strncmp(line, "PHONE ", 6)) {
        return handlePhone(line, nowMs);
    }
    char *cursor = line;
    char *name = word(&cursor);
    if (!name) {
        return control_.respond("ERR invalid-command\n");
    }
    if (!control_.negotiated()) {
        if (strcmp(name, "HELLO") || !cursor || strcmp(cursor, "1")) {
            return control_.respond("ERR protocol-version\n");
        }
        control_.setNegotiated(true);
        return control_.respond("HELLO\t1\n");
    }
    if (!strcmp(name, "STATUS") && !cursor) {
        return handleStatus();
    }
    if (!strcmp(name, "FILES")) { // polled while importing; it logs what it changes
        return files_.handle(cursor, nowMs);
    }
    if (strcmp(name, "LIST") && strcmp(name, "RUNNING")) {
        nbt_logf("command %s", name);
    }
    if (!strcmp(name, "LIST") && !cursor) {
        return handleList();
    }
    if (!strcmp(name, "QUIT") && !cursor) {
        control_.quit();
        return control_.respond("OK\n");
    }
    if (!strcmp(name, "CANCEL") && !cursor) {
        web_.dropClients(worker_);
        worker_.cancel(nowMs);
        wfd_.restore(nowMs);
        char ignored[32];
        (void)web_.api().local("PHONE CLOSE", nowMs, core::Span<char>(ignored, sizeof(ignored)));
        return control_.respond("OK\n");
    }
    if (!strcmp(name, "STOP") || !strcmp(name, "REMOVE")) {
        char *app = word(&cursor);
        char *mode = word(&cursor);
        const bool fault = mode && !strcmp(name, "STOP") && !strcmp(mode, "fault");
        if (cursor || (mode && !fault)) {
            return control_.respond("ERR invalid-command\n");
        }
        return handleStopRemove(name[0] == 'R', app, fault, nowMs);
    }
    if (!strcmp(name, "UNBLOCK")) {
        char *app = word(&cursor);
        return cursor ? control_.respond("ERR invalid-command\n") : handleUnblock(app);
    }
    if (!strcmp(name, "RUNNING") && (!cursor || !strcmp(cursor, "status"))) {
        return handleRunning(cursor);
    }
    if (!strcmp(name, "AUTOSTART")) {
        return handleAutostart(cursor);
    }
    if (!strcmp(name, "SETTINGS") || !strcmp(name, "SET")) {
        return handleSettings(!strcmp(name, "SET"), cursor);
    }
    if (!strcmp(name, "START")) {
        return handleStart(cursor, nowMs);
    }
    if (!strcmp(name, "SESSION")) {
        return handleSession(cursor, nowMs);
    }
    if (!strcmp(name, "APPROVE")) {
        return handleApprove(cursor, nowMs);
    }
    if (!strcmp(name, "SETUP") || !strcmp(name, "IDRIVE")) {
        return handleSystem(name[0] == 'S', cursor);
    }
    return control_.respond("ERR invalid-command\n");
}

} // namespace worker
} // namespace nbt
