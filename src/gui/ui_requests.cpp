#include "nbt/gui/status_update.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/worker_channel.hpp"

#include "nbt/core/system.hpp"
#include "nbt/gui/phone_ui.hpp"
#include "nbt/gui/reply_parser.hpp"
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
            fail(0, "Cannot list applications");
            return 0;
        }
        if (ReplyParser::parseApps(*this, replyText)) {
            break;
        }
        queued = R_RUNNING;
        return 0;
    case R_RUNNING:
        if (ReplyParser::isError(replyText)) {
            fail(0, "Cannot list applications");
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
            queued = R_PHONE_STATUS;
        }
        return 0;
    case R_SESSION:
        if (ReplyParser::isError(replyText)) {
            pairRequested = 0;
            ViewBuilder::formatText(message, sizeof(message), "%s",
                                    strcmp(replyText, "ERR network-unavailable")
                                        ? "Cannot open session"
                                        : "Wi-Fi network unavailable");
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
                showResult("Cannot pair a phone", "The car's Wi-Fi details are unavailable");
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
            exitRequested = 1;
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
        ViewBuilder::formatText(message, sizeof(message),
                                !strcmp(replyText, "ERR running") ? "%s is already running"
                                : !strcmp(replyText, "ERR screen")
                                    ? "%s: another app has the screen"
                                : !strcmp(replyText, "ERR quarantined") ? "%s blocked after faults"
                                                                        : "Cannot start %s",
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
        ViewBuilder::formatText(message, sizeof(message), "Cannot stop %s", target);
        queued = R_RUNNING;
        return 0;
    case R_REMOVE:
        if (!strcmp(replyText, "OK") || !strcmp(replyText, "ERR missing")) {
            queued = R_LIST; // reload the list without it
            mainMenu(0);
            ViewBuilder::formatText(message, sizeof(message), "%s uninstalled", target);
            return 0;
        }
        if (!ReplyParser::isError(replyText)) {
            break;
        }
        go(Settings);
        ViewBuilder::formatText(message, sizeof(message),
                                !strcmp(replyText, "ERR running") ? "Stop %s first"
                                : !strcmp(replyText, "ERR busy")  ? "Busy; uninstall %s later"
                                                                  : "Cannot uninstall %s",
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
        showResult("Installation failed", "");
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
            if (managerSettings()) {
                ViewBuilder::formatText(message, sizeof(message), "Cannot read settings");
            } else {
                ViewBuilder::formatText(message, sizeof(message), "Cannot read %s settings",
                                        settingsApp);
            }
            return 0;
        }
        if (ReplyParser::parseSettings(*this, replyText)) {
            break;
        }
        if (screen == Apps) {
            go(Settings);
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
            ViewBuilder::formatText(message, sizeof(message), "Cannot save the setting");
        }
        queued = R_SETTINGS;
        return 0;
    case R_UNBLOCK:
        if (!strcmp(replyText, "OK")) {
            quarantinedApp[0] = 0;
            go(Settings);
            ViewBuilder::formatText(message, sizeof(message), "%s unblocked", settingsApp);
            return 0;
        }
        if (!ReplyParser::isError(replyText)) {
            break;
        }
        ViewBuilder::formatText(message, sizeof(message), "Cannot unblock %s", settingsApp);
        return 0;
    default:
        break;
    }
    fail(1, "Worker protocol error");
    clearSecrets();
    return -1;
}

void UiController::abortApp(const char *format) {
    appRun = 0;
    handover = 0;
    const size_t i = appIndex(target);
    mainMenu(i < appCount ? i : 0);
    ViewBuilder::formatText(message, sizeof(message), format, target);
    if (target[0] && !stopping) {
        faultStop = 1;
        queued = R_STOP;
    }
}

void UiController::requestFailed() {
    fail(1, "Worker unavailable");
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
