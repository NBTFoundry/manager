// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/status_update.hpp"
#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/gui/worker_text.hpp"
#include <string.h>
namespace nbt {
namespace gui {
namespace {
/* The result screen, with the worker's reason in the current language. */
void result(UiController &ui, TextId what, const char *reason) {
    char why[sizeof(ui.detail)];
    workerText(why, sizeof(why), reason);
    ui.showResult(text(what), why);
}
} // namespace
void updateUiStatus(UiController &ui) {
    const WorkerStatus &s = ui.status;
    if ((ui.screen == Apps || ui.screen == Waiting || ui.screen == Settings ||
         ui.screen == Choice) &&
        s.state == W_VERIFIED && s.app[0]) {
        strcpy(ui.app, s.app);
        ui.review = s;
        ui.go(Review);
        return;
    }
    const bool ours = ui.app[0] && !strcmp(s.app, ui.app);
    // "Stopping <app>" is still the message shown (Settings) or the stop came
    // from the menu (its row says it): the outcome replaces it.
    const bool stopShown = ui.stopNote[0] && (!strcmp(ui.message, ui.stopNote) ||
                                              (ui.screen == Apps && !ui.message[0]));
    switch (ui.screen) {
    case Apps:
    case Settings:
        if (!ui.stopping) {
            return;
        }
        if (s.state == W_FAILED) {
            if (ui.screen == Settings || stopShown) {
                textf(ui.message, sizeof(ui.message), T_CANNOT_STOP, ui.target);
            }
        } else if (!strcmp(s.message, "Application stopped")) {
            if (ui.screen == Settings) {
                textf(ui.message, sizeof(ui.message), T_APP_STOPPED, ui.target);
            } else if (stopShown) {
                ui.message[0] = 0; // on the menu, its row no longer says "running"
            }
        } else {
            return;
        }
        ui.stopNote[0] = 0;
        ui.stopping = 0;
        ui.queued = R_RUNNING;
        return;
    case Waiting:
    case Pairing:
        if (s.state == W_SESSION && !s.app[0]) {
            return;
        }
        if (s.state == W_OPENED && !s.app[0]) {
            ui.focus = ui.connected ? ui.focus : 0; // one item (Cancel) once connected
            ui.connected = 1;
            return;
        }
        if (s.state == W_VERIFIED && s.app[0]) {
            strcpy(ui.app, s.app);
            ui.review = s;
            ui.go(Review);
            return;
        }
        result(ui, s.state == W_FAILED ? T_UPLOAD_REJECTED : T_SESSION_ENDED, s.message);
        ui.queued = R_CANCEL;
        return;
    case Review:
    case Confirm:
        if (ReplyParser::samePackage(s, ui.review)) {
            return;
        }
        if (s.state == W_VERIFIED && ours) {
            ui.review = s;
            ui.go(Review);
            return;
        }
        result(ui, T_SESSION_ENDED, s.message);
        ui.queued = R_CANCEL;
        return;
    case Progress:
        if (s.state == W_INSTALLING && ours) {
            workerText(ui.detail, sizeof(ui.detail), s.message);
            if (!ui.healthMs && !strcmp(s.message, "Checking application health")) {
                ui.healthMs = ui.clockMs ? ui.clockMs : 1; // the progress bar's start
            }
            return;
        }
        if (s.state == W_INSTALLED && ours) {
            if (strcmp(s.app, "manager")) {
                ui.showResult(text(T_UPDATE_INSTALLED), s.installed);
            } else { // "NBT Manager <version> starts in a moment": the GUI ends then
                result(ui, T_UPDATE_READY, s.message);
                ui.managerRestartMs = (ui.clockMs ? ui.clockMs : 1) + kManagerRestartMs;
            }
        } else {
            result(ui, s.state == W_FAILED ? T_UPDATE_FAILED : T_UPDATE_NOT_INSTALLED, s.message);
        }
        return;
    case Result:
        if (s.message[0]) {
            workerText(ui.detail, sizeof(ui.detail), s.message);
        }
        return;
    default:
        return;
    }
}

} // namespace gui
} // namespace nbt
