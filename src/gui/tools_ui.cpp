// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/tools_ui.hpp"
#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/view_builder.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

void backToSettings(UiController &ui) {
    ui.go(Settings);
    ui.focus = ui.managerEntry(EntryTools);
}

} // namespace

bool toolsPress(UiController &ui, Key key) {
    switch (ui.screen) {
    case Settings:
        if (!ui.managerSettings() || key != KeySelect || ui.focus != ui.managerEntry(EntryTools)) {
            return false;
        }
        ui.message[0] = 0;
        ui.go(Tools);
        return true;
    case Tools:
        if (key == KeyBack) {
            backToSettings(ui);
        } else if (key == KeySelect && ui.focus == kToolRestart) {
            ui.restartFromTools = 1;
            ui.go(RestartIdrive); // setup_ui.cpp asks and sends IDRIVE RESTART
        } else if (key == KeySelect && ui.focus == kToolSsh) {
            ui.go(SshConfirm);
        }
        return true;
    case SshConfirm:
        if (key == KeyBack || ui.focus == 0) {
            ui.go(Tools);
            ui.focus = kToolSsh;
        } else {
            ui.queued = R_SSH_ENABLE;
        }
        return true;
    default:
        return false;
    }
}

bool toolsRequest(const UiController &, int kind, Request &request) {
    if (kind != R_SSH_ENABLE) {
        return false;
    }
    snprintf(request.command, sizeof(request.command), "SSH ENABLE");
    return true;
}

int toolsReply(UiController &ui, int kind, const char *reply) {
    if (kind != R_SSH_ENABLE) {
        return -1;
    }
    if (!strcmp(reply, "OK")) {
        ui.showResult(text(T_SSH_ENABLED), text(T_SSH_UNTIL_RESTART), R_SSH_ENABLE);
        return 0;
    }
    if (!ReplyParser::isError(reply)) {
        return -1;
    }
    ui.showResult(text(T_CANNOT_ENABLE_SSH), "", R_SSH_ENABLE);
    return 0;
}

void toolsView(const UiController &ui, UiView &v) {
    if (ui.screen == SshConfirm) {
        textf(v.title, sizeof(v.title), T_SSH_QUESTION);
        ViewBuilder::formatText(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s", text(T_SSH_RUNS));
        ViewBuilder::formatText(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s",
                                text(T_SSH_UNTIL_RESTART));
        textf(v.items[v.itemCount++], sizeof(v.items[0]), T_NO);
        textf(v.items[v.itemCount++], sizeof(v.items[0]), T_ENABLE_SSH);
        return;
    }
    textf(v.title, sizeof(v.title), T_TOOLS);
    textf(v.items[v.itemCount], sizeof(v.items[0]), T_RESTART_HEAD_UNIT);
    v.itemStyle[v.itemCount++] = ItemAction;
    textf(v.items[v.itemCount], sizeof(v.items[0]), T_ENABLE_SSH);
    v.itemStyle[v.itemCount++] = ItemAction;
}

} // namespace gui
} // namespace nbt
