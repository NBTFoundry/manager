// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/menu_keys.hpp"

#include <string.h>

namespace nbt {
namespace gui {

void advanceHold(UiController &ui, uint64_t nowMs) {
    if (!ui.holding || ui.holdFired || ui.screen != Apps || ui.holdIndex >= ui.appCount ||
        ui.queued || ui.outstanding || nowMs < ui.holdMs || nowMs - ui.holdMs < kHoldMs) {
        return;
    }
    ui.holdFired = 1;
    ui.holding = 0;
    if (!ui.running[ui.holdIndex] || ui.stopping) {
        return;
    }
    strcpy(ui.target, ui.apps[ui.holdIndex]);
    ui.faultStop = 0;
    ui.queued = R_STOP;
    ui.message[0] = 0; // its row says "stopping"; stopNote marks a stop from the menu
    textf(ui.stopNote, sizeof(ui.stopNote), T_STOPPING_APP, ui.target);
}

namespace {
// "Stopping <app>" left from Settings: the row says it.
bool stopMessage(const UiController &ui) {
    return ui.stopNote[0] && !strcmp(ui.message, ui.stopNote);
}
} // namespace

const char *menuNote(const UiController &ui) {
    if (ui.trialSinceMs) {
        return NULL;
    }
    if (ui.message[0] && !stopMessage(ui)) {
        return ui.message;
    }
    if (ui.notice[0]) {
        return ui.notice;
    }
    return ui.captureNote[0] && strcmp(ui.captureNote, ui.captureSeen) ? ui.captureNote : NULL;
}

void closeMenuNote(UiController &ui) {
    if (ui.message[0] && !stopMessage(ui)) {
        ui.message[0] = 0;
    } else if (ui.notice[0]) {
        ui.notice[0] = 0;
    } else {
        memcpy(ui.captureSeen, ui.captureNote, sizeof(ui.captureSeen));
    }
}

int optionKey(UiController &ui) {
    if (ui.screen != Apps || ui.inputBusy() || ui.stopping) {
        return 0;
    }
    ui.holding = 0;
    ui.message[0] = 0;
    strcpy(ui.settingsApp, ui.focus < ui.appCount ? ui.apps[ui.focus] : "manager");
    ui.queued = R_SETTINGS;
    return 1;
}

} // namespace gui
} // namespace nbt
