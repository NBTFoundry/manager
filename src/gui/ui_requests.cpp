// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/status_update.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/worker_channel.hpp"

#include "nbt/core/system.hpp"
#include "nbt/gui/phone_ui.hpp"
#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/setup_ui.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/gui/worker_client.hpp"
#include "nbt/log.h"

#include <string.h>

namespace nbt {
namespace gui {

int UiController::parseStatus(const char *replyText, WorkerStatus &statusOut) {
    return ReplyParser::parseStatus(replyText, statusOut);
}

int UiController::reply(const char *replyText, uint64_t nowMs) {
    WorkerStatus workerStatus;
    if (!replyText || !outstanding) {
        return -1;
    }
    const int kind = outstanding;
    outstanding = R_NONE;
    if (kind >= R_PHONE_STATUS) {
        return phoneReply(*this, kind, replyText, nowMs);
    }
    switch (kind) {
    case R_LIST:
        if (ReplyParser::isError(replyText)) {
            fail(0, text(T_CANNOT_LIST));
            return 0;
        }
        if (ReplyParser::parseApps(*this, replyText)) {
            break;
        }
        queued = R_RUNNING;
        return 0;
    case R_RUNNING:
        if (ReplyParser::isError(replyText)) {
            fail(0, text(T_CANNOT_LIST));
            return 0;
        }
        if (ReplyParser::parseRunning(*this, replyText)) {
            break;
        }
        if (appRun) { // the application on the panel: running, then gone (it ended)
            const size_t i = appIndex(target);
            appRun = i < appCount && running[i] ? 2 : appRun == 2 ? 3 : appRun;
        }
        if (screen == Loading) {
            go(Apps);
        }
        if (focus >= itemCount()) {
            focus = itemCount() - 1;
        }
        if (screen == Apps && !queued && !holding) {
            // SETUP first, so a manager on trial shows its check at once, the
            // menu busy until the answer (setup_ui.hpp); else which applications
            // run only their service.
            queued = setupNow(*this, nowMs) ? R_SETUP : R_SERVICES;
        }
        return 0;
    case R_SERVICES:
        if (ReplyParser::isError(replyText) || ReplyParser::parseServices(*this, replyText)) {
            memset(service, 0, sizeof(service));
        }
        if (screen == Apps && !queued && !holding) {
            queued = R_PHONE_STATUS;
        }
        return 0;
    case R_SESSION:
        if (ReplyParser::isError(replyText)) {
            pairRequested = 0;
            textf(message, sizeof(message),
                  strcmp(replyText, "ERR network-unavailable") ? T_CANNOT_OPEN_SESSION
                                                               : T_WIFI_UNAVAILABLE);
            return 0;
        }
        if (ReplyParser::parseSession(*this, replyText)) {
            break;
        }
        nextPollMs = nowMs;
        pairingDeadlineMs = nowMs <= UINT64_MAX - kPairingMs ? nowMs + kPairingMs : nowMs;
        pairingLeftS = kPairingMs / 1000u;
        connected = 0;
        if (pairRequested) { // from Settings: straight to the Wi-Fi code
            pairRequested = 0;
            if (hasWifi()) {
                queued = R_PHONE_OPEN;
            } else {
                showResult(text(T_CANNOT_PAIR), text(T_CAR_WIFI_UNAVAILABLE));
                queued = R_CANCEL;
            }
        }
        return 0;
    case R_STATUS:
        if (ReplyParser::parseStatus(replyText, workerStatus)) {
            break;
        }
        status = workerStatus;
        updateUiStatus(*this);
        if (!queued && screen != Apps && screen != Loading && screen != Progress &&
            screen != Result && screen != Error) {
            queued = R_PHONE_STATUS;
        }
        return 0;
    case R_START:
        if (!strcmp(replyText, "OK")) {
            // Started without the screen (it runs in the background): the
            // manager stays open, its list showing the application running.
            // A start with the car never comes here (the worker's --autostart).
            message[0] = 0; // its row shows it running
            queued = R_RUNNING;
            return 0;
        }
        if (!strcmp(replyText, "OK screen")) {
            handover = 1; // the GUI hands the panel over, keeping iDrive held
            return 0;
        }
        if (!ReplyParser::isError(replyText)) {
            break;
        }
        if (!strcmp(replyText, "ERR quarantined")) {
            strcpy(quarantinedApp, target);
        }
        textf(message, sizeof(message),
              !strcmp(replyText, "ERR running")        ? T_ALREADY_RUNNING
              : !strcmp(replyText, "ERR screen")       ? T_SCREEN_TAKEN
              : !strcmp(replyText, "ERR quarantined")  ? T_BLOCKED_FAULTS
              : !strcmp(replyText, "ERR incompatible") ? T_APP_NEEDS_UPDATE
                                                       : T_CANNOT_START,
              target);
        queued = R_RUNNING;
        return 0;
    case R_STOP:
        if (!strcmp(replyText, "OK")) {
            stopping = 1;
            nextPollMs = nowMs;
            return 0;
        }
        if (!ReplyParser::isError(replyText)) {
            break;
        }
        textf(message, sizeof(message), T_CANNOT_STOP, target);
        queued = R_RUNNING;
        return 0;
    case R_REMOVE:
        if (!strcmp(replyText, "OK") || !strcmp(replyText, "ERR missing")) {
            queued = R_LIST; // reload the list without it
            mainMenu(0);
            textf(message, sizeof(message), T_APP_UNINSTALLED_NAMED, target);
            return 0;
        }
        if (!ReplyParser::isError(replyText)) {
            break;
        }
        go(Settings);
        focus = uninstallItem(); // as after No: on Uninstall
        textf(message, sizeof(message),
              !strcmp(replyText, "ERR running") ? T_STOP_FIRST
              : !strcmp(replyText, "ERR busy")  ? T_BUSY_UNINSTALL_LATER
                                                : T_CANNOT_UNINSTALL,
              target);
        queued = R_RUNNING;
        return 0;
    case R_APPROVE:
        if (!strcmp(replyText, "OK installing")) {
            detail[0] = 0;
            nextPollMs = nowMs;
            return 0;
        }
        if (!ReplyParser::isError(replyText)) {
            break;
        }
        showResult(text(T_INSTALL_FAILED), "");
        queued = R_STATUS;
        return 0;
    case R_CANCEL:
        if (strcmp(replyText, "OK")) {
            break;
        }
        return 0;
    case R_SETTINGS:
        if (ReplyParser::isError(replyText)) {
            if (screen != Apps) {
                leaveSettings();
            }
            textf(message, sizeof(message),
                  managerSettings() ? T_CANNOT_READ_SETTINGS : T_CANNOT_READ_APP_SETTINGS,
                  settingsApp);
            return 0;
        }
        if (ReplyParser::parseSettings(*this, replyText)) {
            break;
        }
        if (screen == Apps) {
            go(Settings);
        }
        if (settingsUpdate && !message[0]) { // its page opens, to uninstall it
            textf(message, sizeof(message), T_APP_NEEDS_UPDATE, settingsApp);
        }
        if (focus >= itemCount()) {
            focus = 0;
        }
        return 0;
    case R_SET:
        if (strcmp(replyText, "OK") && !ReplyParser::isError(replyText)) {
            break;
        }
        go(Settings);
        focus = settingIndex;
        if (strcmp(replyText, "OK")) {
            textf(message, sizeof(message), T_CANNOT_SAVE_SETTING);
        }
        queued = R_SETTINGS;
        return 0;
    case R_UNBLOCK:
        if (!strcmp(replyText, "OK")) {
            quarantinedApp[0] = 0;
            go(Settings);
            textf(message, sizeof(message), T_APP_UNBLOCKED, settingsApp);
            return 0;
        }
        if (!ReplyParser::isError(replyText)) {
            break;
        }
        textf(message, sizeof(message), T_CANNOT_UNBLOCK, settingsApp);
        return 0;
    default:
        break;
    }
    fail(1, text(T_WORKER_PROTOCOL_ERROR));
    clearSecrets();
    return -1;
}

void UiController::abortApp(TextId why) {
    appRun = 0;
    handover = 0;
    const size_t i = appIndex(target);
    mainMenu(i < appCount ? i : 0);
    if (why == T_STOPPING_APP) { // its row says "stopping"
        message[0] = 0;
        textf(stopNote, sizeof(stopNote), why, target);
    } else {
        textf(message, sizeof(message), why, target);
    }
    if (target[0] && !stopping) {
        faultStop = 1;
        queued = R_STOP;
    }
}

void UiController::requestFailed() {
    fail(1, text(T_WORKER_UNAVAILABLE));
    clearSecrets();
}

void UiController::pump(WorkerChannel &channel) {
    char replyText[kReplyMax];
    char command[kCommandMax];
    bool ok = false;
    if (channel.take(replyText, sizeof(replyText), &ok, command, sizeof(command))) {
        if (ok) {
            const core::Result<uint64_t> replyTime = core::monotonicMs();
            if (replyTime.get() && !reply(replyText, replyTime.value())) {
                return;
            }
        }
        nbt_logf("worker request %.*s failed", static_cast<int>(strcspn(command, " ")), command);
        channel.stop(); // a failed or garbled reply ends the channel, as it did the client
        requestFailed();
        return;
    }
    Request request;
    const core::Result<uint64_t> nowResult = core::monotonicMs();
    if (channel.busy() || !nowResult.get() || nextRequest(nowResult.value(), request) != 1) {
        return;
    }
    if (!channel.submit(request)) {
        nbt_logf("worker request %.*s not sent", static_cast<int>(strcspn(request.command, " ")),
                 request.command);
        requestFailed();
    }
}

} // namespace gui
} // namespace nbt
