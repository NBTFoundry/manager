// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/ui_controller.hpp"

#include "nbt/gui/files_ui.hpp"
#include "nbt/gui/language_ui.hpp"
#include "nbt/gui/phone_ui.hpp"
#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/setup_ui.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

bool polling(const UiController &ui) {
    return ui.screen == PhoneConfirm || (ui.screen == Uninstall && ui.phoneAction[0]) ||
           ui.screen == Waiting || ui.screen == Pairing || ui.screen == Settings ||
           ui.screen == Choice || ui.screen == Review || ui.screen == Confirm ||
           ui.screen == Progress || (ui.screen == Apps && ui.stopping) || filesPolling(ui);
}

void advanceHold(UiController &ui, uint64_t nowMs) {
    if (!ui.holding || ui.holdFired || ui.screen != Apps || ui.holdIndex >= ui.appCount ||
        ui.queued || ui.outstanding || nowMs < ui.holdMs || nowMs - ui.holdMs < kHoldMs) {
        return;
    }
    ui.holdFired = 1;
    ui.holding = 0;
    strcpy(ui.settingsApp, ui.apps[ui.holdIndex]);
    ui.queued = R_SETTINGS;
}

} // namespace

int UiController::press(Key key) {
    if (key != KeyUp && key != KeyDown && key != KeySelect && key != KeyBack) {
        return -1;
    }
    const size_t items = itemCount();
    if (key == KeyUp || key == KeyDown) {
        holding = 0;
        if (key == KeyUp && focus > 0) {
            --focus;
            return 1;
        }
        if (key == KeyDown && focus + 1 < items) {
            ++focus;
            return 1;
        }
        return 0;
    }
    if (inputBusy() || stopping) {
        return 0;
    }
    if (phonePress(*this, key) || statusPress(*this, key) || languagePress(*this, key)) {
        return 1;
    }
    switch (screen) {
    case Apps:
        if (key == KeyBack) {
            exitRequested = 1;
            return 1;
        }
        message[0] = 0;
        if (focus < appCount) {
            holding = 1;
            holdIndex = focus;
            holdMs = clockMs;
            holdFired = 0;
            return 1;
        }
        strcpy(settingsApp, "manager");
        queued = R_SETTINGS;
        return 1;
    case Settings:
        if (managerSettings() && key == KeySelect && focus == managerEntry(EntryPair)) {
            pairRequested = 1; // the session gives the Wi-Fi details for the code
            queued = R_SESSION;
            textf(message, sizeof(message), T_OPENING_PAIRING);
            return 1;
        }
        if (managerSettings() && key == KeySelect && focus == managerEntry(EntryPhones)) {
            queued = R_PHONES;
            return 1;
        }
        if (key == KeySelect && focus == settings.count && stopEntry()) {
            strcpy(target, settingsApp);
            faultStop = 0;
            queued = R_STOP;
            textf(message, sizeof(message), T_STOPPING_APP, target);
            memcpy(stopNote, message, sizeof(stopNote));
            return 1;
        }
        if (key == KeySelect && unblockEntry() && focus == unblockItem()) {
            queued = R_UNBLOCK;
            return 1;
        }
        if (key == KeySelect && !managerSettings() && focus == uninstallItem()) {
            go(Uninstall); // focus 0 is "No"
            return 1;
        }
        if (key == KeyBack || focus >= settings.count) {
            leaveSettings();
            return 1;
        }
        message[0] = 0;
        settingIndex = focus;
        go(Choice);
        {
            const nbt_setting *s = &settings.items[settingIndex];
            for (size_t i = 0; i < s->choice_count; ++i) {
                focus = !strcmp(s->choices[i], s->value) ? i : focus;
            }
        }
        return 1;
    case Choice:
        if (key == KeyBack || settingIndex >= settings.count ||
            focus >= settings.items[settingIndex].choice_count) {
            go(Settings);
            focus = settingIndex;
            return 1;
        }
        strcpy(settingValue, settings.items[settingIndex].choices[focus]);
        queued = R_SET;
        return 1;
    case Pairing:
        leaveSession(text(T_PAIRING_CLOSED));
        return 1;
    case Uninstall:
        if (phoneAction[0]) {
            queued = key == KeyBack || focus == 0 ? R_PHONE_NO : R_PHONE_YES;
            return 1;
        }
        if (key == KeyBack || focus == 0) {
            go(Settings);
            focus = uninstallItem();
            return 1;
        }
        strcpy(target, settingsApp);
        queued = R_REMOVE;
        textf(message, sizeof(message), T_UNINSTALLING_APP, target);
        return 1;
    case Review:
        if (key == KeyBack || focus == 0) {
            leaveSession(text(T_UPDATE_REJECTED));
            return 1;
        }
        recovery = focus == 2;
        go(Confirm);
        return 1;
    case Confirm:
        if (key == KeyBack || focus == 0) {
            go(Review);
            return 1;
        }
        if (!ReplyParser::samePackage(status, review)) {
            if (status.state != W_VERIFIED || strcmp(status.app, app)) {
                leaveSession(text(T_SESSION_ENDED));
                return 1;
            }
            review = status;
            go(Review);
            return 1;
        }
        go(Progress);
        healthMs = 0;
        queued = R_APPROVE;
        textf(message, sizeof(message), T_INSTALLING_APP, review.app, review.proposed);
        detail[0] = 0;
        return 1;
    case Result:
        queued = R_LIST; // an installation may have added an application
        mainMenu(appCount);
        message[0] = 0;
        return 1;
    case Error:
        if (fatal || key == KeyBack) {
            exitRequested = 1;
            return 1;
        }
        go(Loading);
        queued = R_LIST;
        return 1;
    default:
        return 0;
    }
}

int UiController::release(Key key) {
    if (key != KeySelect || !holding) {
        return 0;
    }
    holding = 0;
    if (holdFired || screen != Apps || holdIndex >= appCount || inputBusy()) {
        return 0;
    }
    strcpy(target, apps[holdIndex]);
    queued = R_START;
    textf(message, sizeof(message), T_STARTING_APP, target);
    return 1;
}

int UiController::nextRequest(uint64_t nowMs, Request &request) {
    char *command = request.command;
    const size_t size = sizeof(request.command);
    int n = 0;

    command[0] = 0;
    clockMs = nowMs;
    advanceHold(*this, nowMs);
    if (outstanding || fatal) {
        return 0;
    }
    if (screen == Pairing && !queued && !connected) {
        if (nowMs >= pairingDeadlineMs) {
            leaveSession(text(T_PAIRING_TIMEOUT));
        } else {
            pairingLeftS = static_cast<unsigned>((pairingDeadlineMs - nowMs + 999u) / 1000u);
        }
    }
    int kind = queued;
    if (!kind) {
        if (exitRequested || nowMs < nextPollMs) {
            return 0;
        }
        if (screen == Apps && !holding && !stopping) {
            kind = setupNow(*this, nowMs) ? R_SETUP : R_RUNNING;
        } else if (polling(*this)) {
            kind = filesPolling(*this) ? R_FILES_USB : R_STATUS;
        } else {
            return 0;
        }
    }
    request.timeoutMs = kind == R_SESSION ? 15000 : 5000;
    if (!phoneRequest(*this, kind, request)) {
        switch (kind) {
        case R_LIST:
            n = snprintf(command, size, "LIST");
            break;
        case R_RUNNING:
            n = snprintf(command, size, "RUNNING status");
            break;
        case R_STATUS:
            n = snprintf(command, size, "STATUS");
            break;
        case R_CANCEL:
            n = snprintf(command, size, "CANCEL");
            break;
        case R_START:
            n = snprintf(command, size, "START %s", target);
            break;
        case R_SETTINGS:
            n = snprintf(command, size, "SETTINGS %s live", settingsApp);
            break;
        case R_SET:
            if (settingIndex >= settings.count) {
                return -1;
            }
            n = snprintf(command, size, "SET %s %s %s", settingsApp,
                         settings.items[settingIndex].key, settingValue);
            break;
        case R_STOP:
        case R_REMOVE:
            n = snprintf(command, size, "%s %s%s", kind == R_STOP ? "STOP" : "REMOVE", target,
                         (kind == R_STOP && faultStop) ? " fault" : "");
            if (kind == R_STOP) {
                faultStop = 0;
            }
            break;
        case R_UNBLOCK:
            n = snprintf(command, size, "UNBLOCK %s", settingsApp);
            break;
        case R_SESSION:
            n = snprintf(command, size, "SESSION %s %u", interfaceName, port);
            break;
        case R_APPROVE:
            n = snprintf(command, size, "APPROVE %s %s", review.digest,
                         recovery ? "recovery" : "normal");
            break;
        default:
            return -1;
        }
        if (n < 0 || static_cast<size_t>(n) >= size) {
            command[0] = 0;
            return -1;
        }
    }
    queued = R_NONE;
    outstanding = kind;
    if ((kind == R_STATUS || kind == R_RUNNING || kind == R_PHONE_STATUS || kind == R_FILES_USB) &&
        nowMs <= UINT64_MAX - kPollMs) {
        nextPollMs = nowMs + kPollMs;
    }
    return 1;
}

} // namespace gui
} // namespace nbt
