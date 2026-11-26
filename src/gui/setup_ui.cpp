// SPDX-License-Identifier: MPL-2.0
/* The manager's own installation. Once the list shows, the GUI asks the worker
 * to bring the launcher and the iDrive plugin to the confirmed release (SETUP);
 * while an update is on trial it asks again after the health check. When the
 * plugin was replaced, iDrive loads it only after a restart: the driver is
 * asked, "Later" focused, and only "Restart head unit" sends IDRIVE RESTART.
 * That request runs OnOffDSICommander appreset, which restarts the whole head
 * unit (the user, 2026-11-18), not iDrive alone. While the update is on trial
 * the list shows a modal with the check's progress (the launcher confirms
 * after NBT_HEALTHY_MS), so the question follows in the same sitting. */
#include "nbt/gui/setup_ui.hpp"
#include "nbt/gui/autostart_ui.hpp"
#include "nbt/gui/files_ui.hpp"
#include "nbt/gui/language_ui.hpp"
#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/shortcut_ui.hpp"
#include "nbt/gui/tools_ui.hpp"
#include "nbt/log.h"
#include <stdio.h>
#include <string.h>
namespace nbt {
namespace gui {

bool setupNow(const UiController &ui, uint64_t nowMs) {
    return ui.setupDue && ui.screen == Apps && !ui.holding && nowMs >= ui.setupAtMs;
}

bool setupPress(UiController &ui, Key key) {
    if (ui.screen != RestartIdrive) {
        return false;
    }
    if (key == KeyBack || ui.focus == 0) {
        if (ui.restartFromTools) { // Settings > Tools: back to the menu it came from
            ui.restartFromTools = 0;
            ui.go(Tools);
            ui.focus = kToolRestart;
        } else {
            ui.go(Apps);
        }
    } else {
        ui.queued = R_IDRIVE_RESTART; // restartFromTools stays for the view and the result
    }
    return true;
}

bool setupRequest(int kind, Request &request) {
    if (kind == R_SETUP) {
        snprintf(request.command, sizeof(request.command), "SETUP");
        request.timeoutMs = 120000; // a plugin replacement copies and syncs about 1 MB
        return true;
    }
    if (kind == R_IDRIVE_RESTART) {
        snprintf(request.command, sizeof(request.command), "IDRIVE RESTART");
        return true;
    }
    return false;
}

int setupReply(UiController &ui, int kind, const char *reply, uint64_t now) {
    if (kind >= R_FILES) {
        return filesReply(ui, kind, reply, now);
    }
    if (kind == R_IDRIVE_RESTART) {
        /* Titled Tools only when asked from there; after a plugin update it
         * stays an installation's result. */
        const int resultKind = ui.restartFromTools ? R_IDRIVE_RESTART : 0;
        if (!strcmp(reply, "OK")) {
            ui.showResult(text(T_RESTARTING), text(T_WHOLE_SYSTEM_RESTARTS), resultKind);
        } else {
            ui.showResult(text(T_CANNOT_RESTART), text(T_SWITCH_OFF_ON), resultKind);
        }
        return 0;
    }
    if (kind == R_SSH_ENABLE) {
        return toolsReply(ui, kind, reply);
    }
    if (kind >= R_SELFTEST_PREPARE && kind <= R_SELFTEST_LAST) {
        return selfTestReply(ui, kind, reply);
    }
    if (kind == R_LANGUAGE) {
        return languageReply(ui, reply);
    }
    if (kind == R_AUTOSTART) {
        return autostartReply(ui, reply);
    }
    if (kind == R_SHORTCUT) {
        return shortcutReply(ui, reply);
    }
    if (kind != R_SETUP) {
        return -1;
    }
    ui.setupDue = 0;
    const bool trial = !strcmp(reply, "SETUP\ttrial");
    if (ui.trialSinceMs && !trial && strcmp(reply, "SETUP\tbusy")) {
        ui.trialSinceMs = 0; // confirmed: the modal makes way for the result
        textf(ui.message, sizeof(ui.message), T_MANAGER_UPDATED, ui.version);
    }
    if (trial || !strcmp(reply, "SETUP\tbusy")) {
        // On trial the page shows the check, asked again until it is over.
        if (trial && !ui.trialSinceMs) {
            ui.trialSinceMs = now ? now : 1;
        }
        ui.setupDue = 1;
        ui.setupAtMs = now + (trial ? kTrialPollMs : kSetupRetryMs);
    } else if (!strcmp(reply, "SETUP\tplugin") || !strcmp(reply, "SETUP\tboth")) {
        nbt_logf("the iDrive entry was updated; asking to restart the head unit");
        if (ui.screen == Apps) {
            ui.go(RestartIdrive);
        }
    } else if (ReplyParser::isError(reply)) {
        nbt_logf("the launcher or the iDrive entry could not be updated");
    } else if (strcmp(reply, "SETUP\tcurrent") && strcmp(reply, "SETUP\tlauncher")) {
        return -1;
    }
    return 0;
}

void setupView(const UiController &ui, UiView &v) {
    textf(v.title, sizeof(v.title), T_RESTART_QUESTION);
    if (!ui.restartFromTools) { // Tools asks without the plugin update's lines
        textf(v.lines[v.lineCount++], sizeof(v.lines[0]), T_ENTRY_UPDATED);
        textf(v.lines[v.lineCount++], sizeof(v.lines[0]), T_LOADS_ON_RESTART);
    }
    textf(v.lines[v.lineCount++], sizeof(v.lines[0]), T_RESTART_LIKE_OFF_ON);
    textf(v.items[v.itemCount++], sizeof(v.items[0]), T_LATER);
    textf(v.items[v.itemCount++], sizeof(v.items[0]), T_RESTART_HEAD_UNIT);
}

} // namespace gui
} // namespace nbt
