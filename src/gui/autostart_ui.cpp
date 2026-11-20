// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/autostart_ui.hpp"

#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/log.h"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {

bool autostartPress(UiController &ui, Key key) {
    if (ui.screen != Settings || ui.managerSettings() || key != KeySelect ||
        ui.focus != ui.autostartItem()) {
        return false;
    }
    ui.message[0] = 0;
    ui.autostartPick = !ui.autostart;
    ui.queued = R_AUTOSTART;
    return true;
}

bool autostartRequest(const UiController &ui, int kind, Request &request) {
    if (kind != R_AUTOSTART) {
        return false;
    }
    snprintf(request.command, sizeof(request.command), "AUTOSTART %s %s", ui.settingsApp,
             ui.autostartPick ? "on" : "off");
    return true;
}

int autostartReply(UiController &ui, const char *reply) {
    const bool saved = !strcmp(reply, "OK");
    if (!saved && !ReplyParser::isError(reply)) {
        ui.fail(1, text(T_WORKER_PROTOCOL_ERROR));
        return -1;
    }
    ui.focus = ui.autostartItem();
    if (saved) {
        nbt_logf("%s starts with the car: %s", ui.settingsApp, ui.autostartPick ? "on" : "off");
    } else {
        textf(ui.message, sizeof(ui.message), T_CANNOT_SAVE_SETTING);
    }
    ui.queued = R_SETTINGS; // the page as the worker now has it
    return 0;
}

void autostartEntry(const UiController &ui, char *out, size_t size) {
    textf(out, size, T_SETTING_ROW, text(T_START_WITH_CAR),
          text(ui.autostart ? T_VALUE_ON : T_VALUE_OFF));
}

bool autostartNote(const UiController &ui, char *out, size_t size) {
    if (ui.managerSettings() || !ui.autostart || !ui.autostartPaused) {
        return false;
    }
    textf(out, size, T_AUTOSTART_PAUSED, ui.settingsApp);
    return true;
}

} // namespace gui
} // namespace nbt
