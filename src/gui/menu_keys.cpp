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
    textf(ui.message, sizeof(ui.message), T_STOPPING_APP, ui.target);
    memcpy(ui.stopNote, ui.message, sizeof(ui.stopNote));
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
