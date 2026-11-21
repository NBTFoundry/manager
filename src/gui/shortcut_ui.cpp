// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/shortcut_ui.hpp"

#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/shortcut_record.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/log.h"
#include "nbt/manager_settings.h"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {

bool shortcutPress(UiController &ui, Key key) {
    if (ui.screen != Settings || ui.managerSettings() || key != KeySelect ||
        ui.focus != ui.shortcutItem()) {
        return false;
    }
    ui.message[0] = 0;
    ui.shortcutPick = (ui.shortcut + 1) % (NBT_SHORTCUTS + 1);
    ui.queued = R_SHORTCUT;
    return true;
}

bool shortcutRequest(const UiController &ui, int kind, Request &request) {
    if (kind != R_SHORTCUT) {
        return false;
    }
    // Moving on from the last entry frees it; otherwise the worker frees the
    // application's previous entry itself.
    if (ui.shortcutPick) {
        snprintf(request.command, sizeof(request.command), "SET manager shortcut%d %s",
                 ui.shortcutPick, ui.settingsApp);
    } else {
        snprintf(request.command, sizeof(request.command), "SET manager shortcut%d off",
                 ui.shortcut);
    }
    return true;
}

int shortcutReply(UiController &ui, const char *reply) {
    const bool saved = !strcmp(reply, "OK");
    if (!saved && !ReplyParser::isError(reply)) {
        ui.fail(1, text(T_WORKER_PROTOCOL_ERROR));
        return -1;
    }
    ui.focus = ui.shortcutItem();
    if (saved) {
        nbt_logf("%s: App shortcut %d", ui.settingsApp, ui.shortcutPick);
    } else {
        textf(ui.message, sizeof(ui.message), T_CANNOT_SAVE_SETTING);
    }
    ui.queued = R_SETTINGS; // the page as the worker now has it
    return 0;
}

void shortcutEntry(const UiController &ui, char *out, size_t size) {
    char number[4];
    snprintf(number, sizeof(number), "%d", ui.shortcut);
    textf(out, size, T_SETTING_ROW, text(T_APP_SHORTCUT), ui.shortcut ? number : text(T_VALUE_OFF));
}

bool shortcutNote(const UiController &ui, char *out, size_t size) {
    if (ui.managerSettings() || !ui.shortcut) {
        return false;
    }
    char number[4];
    snprintf(number, sizeof(number), "%d", ui.shortcut);
    textf(out, size, T_APP_SHORTCUT_NOTE, number);
    return true;
}

void shortcutStartPoll(UiController &ui, uint64_t nowMs) {
    static uint64_t nextMs = 0;
    if (nowMs < nextMs || ui.screen != Apps || ui.queued || ui.inputBusy() || ui.stopping) {
        return;
    }
    nextMs = nowMs + 300;
    ShortcutRecord record;
    if (!shortcutRecordRead(shortcutRecordPath(), record) || !shortcutRecordDue(record, 0, nowMs)) {
        return;
    }
    const bool start = record.word == ShortcutStart && ui.appIndex(record.app) < ui.appCount;
    record.word = ShortcutTaken;
    if (!shortcutRecordWrite(shortcutRecordPath(), record) || !start) {
        return;
    }
    snprintf(ui.target, sizeof(ui.target), "%s", record.app);
    nbt_logf("App shortcut: starting %s", ui.target);
    ui.queued = R_START;
    ui.message[0] = 0; // its row says "starting"
}

} // namespace gui
} // namespace nbt
