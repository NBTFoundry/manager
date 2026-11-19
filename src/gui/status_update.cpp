#include "nbt/gui/status_update.hpp"
#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/view_builder.hpp"
#include <string.h>
namespace nbt {
namespace gui {
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
    switch (ui.screen) {
    case Apps:
    case Settings:
        if (!ui.stopping) {
            return;
        }
        if (s.state == W_FAILED) {
            if (ui.screen == Settings || !strncmp(ui.message, "Stopping ", 9)) {
                ViewBuilder::formatText(ui.message, sizeof(ui.message), "Cannot stop %s",
                                        ui.target);
            }
        } else if (!strcmp(s.message, "Application stopped")) {
            if (ui.screen == Settings || !strncmp(ui.message, "Stopping ", 9)) {
                ViewBuilder::formatText(ui.message, sizeof(ui.message), "%s stopped", ui.target);
            }
        } else {
            return;
        }
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
        ui.showResult(s.state == W_FAILED ? "Upload rejected" : "Session ended", s.message);
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
        ui.showResult("Session ended", s.message);
        ui.queued = R_CANCEL;
        return;
    case Progress:
        if (s.state == W_INSTALLING && ours) {
            ViewBuilder::formatText(ui.detail, sizeof(ui.detail), "%s", s.message);
            if (!ui.healthMs && !strcmp(s.message, "Checking application health")) {
                ui.healthMs = ui.clockMs ? ui.clockMs : 1; // the progress bar's start
            }
            return;
        }
        if (s.state == W_INSTALLED && ours) {
            ui.showResult(strcmp(s.app, "manager") ? "Update installed" : "Update ready",
                          s.installed);
        } else {
            ui.showResult(s.state == W_FAILED ? "Update failed" : "Update not installed",
                          s.message);
        }
        return;
    case Result:
        if (s.message[0]) {
            ViewBuilder::formatText(ui.detail, sizeof(ui.detail), "%s", s.message);
        }
        return;
    default:
        return;
    }
}

} // namespace gui
} // namespace nbt
