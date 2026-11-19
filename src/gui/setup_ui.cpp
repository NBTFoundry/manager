/* The manager's own installation. Once the list shows, the GUI asks the worker
 * to bring the launcher and the iDrive plugin to the confirmed release (SETUP);
 * while an update is on trial it asks again after the health check. When the
 * plugin was replaced, iDrive loads it only after a restart: the driver is
 * asked, "Later" focused, and only "Restart head unit" sends IDRIVE RESTART.
 * That request runs OnOffDSICommander appreset, which restarts the whole head
 * unit (the user, 2026-11-18), not iDrive alone. */
#include "nbt/gui/setup_ui.hpp"
#include "nbt/gui/reply_parser.hpp"
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
        ui.go(Apps);
    } else {
        ui.queued = R_IDRIVE_RESTART;
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
    if (kind == R_IDRIVE_RESTART) {
        if (!strcmp(reply, "OK")) {
            ui.showResult("Restarting the head unit", "The whole system restarts");
        } else {
            ui.showResult("Cannot restart", "Switch the car off and on instead");
        }
        return 0;
    }
    if (kind != R_SETUP) {
        return -1;
    }
    ui.setupDue = 0;
    if (!strcmp(reply, "SETUP\ttrial") || !strcmp(reply, "SETUP\tbusy")) {
        ui.setupDue = 1;
        ui.setupAtMs = now + kSetupRetryMs;
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

void setupView(UiView &v) {
    snprintf(v.title, sizeof(v.title), "Restart head unit?");
    snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "NBT Manager's iDrive entry was updated");
    snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "It loads when the head unit restarts");
    snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]),
             "The whole system restarts, as car off/on");
    snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "Later");
    snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "Restart head unit");
}

} // namespace gui
} // namespace nbt
