// SPDX-License-Identifier: MPL-2.0
/* The GUI session and the plugin's hold on iDrive: its show and aside
 * signals, the panel handed to an application, and the hand-over to the next
 * GUI around a manager update (src/launcher/update_handover.cpp). */
#include "nbt/core/system.hpp"
#include "nbt/display/layer_manager.hpp"
#include "nbt/gui/gui_session.hpp"
#include "nbt/log.h"
#include "nbt/most_input.h"

#include <errno.h>
#include <unistd.h>

namespace nbt {
namespace gui {

int GuiSession::pollHoldSignal(bool &shouldExit) {
    shouldExit = false;
    HoldSignal signal = SignalShow;
    if (!plugin_.next(signal)) {
        return 0;
    }
    const bool show = signal == SignalShow;
    nbt_logf("iDrive signal %s%s", show ? "show" : "aside",
             !show && !ui_.installing() ? "; closing" : "");
    if (!show) {
        capture_.setOff();
        nbt_most_input_log_recent("iDrive took the screen");
        pauseShownApp();
    }
    if (!show && !ui_.installing()) {
        exitReason_ = "iDrive left the manager";
        shouldExit = true;
        return 0;
    }
    if (!show) {
        panel_.clear();
        hidden_ = true;
        return screen_.setVisible(false).ok() ? 0 : -1;
    }
    return hidden_ && !panel_.active(display::kAppSurface) ? showManager() : 0;
}
int GuiSession::showManager() {
    if (drawUi(shown_) || !screen_.setVisible(true).ok()) {
        return -1;
    }
    hidden_ = false;
    panel_.place(display::kManagerSurface, nowMs_);
    if (deadlineMs_ < nowMs_ + kIdleMs) {
        deadlineMs_ = nowMs_ + kIdleMs;
    }
    return 0;
}
void GuiSession::handOver() {
    nbt_logf("handing the panel to %s", ui_.target);
    panel_.place(display::kAppSurface, nowMs_);
    (void)screen_.setVisible(false);
    hidden_ = true;
}

// After a manager update the launcher kept iDrive held for this GUI: the
// plugin's "show" was for the GUI before, and no new one comes.
int GuiSession::showHandedOver() {
    if (!config_.handover || !plugin_.shownAtStart()) {
        return 0;
    }
    nbt_logf("started after the manager update; shown at once");
    return showManager();
}

// An approved manager update: the launcher starts it once this GUI ends.
bool GuiSession::endForUpdate() {
    if (config_.readyFd < 0 || !ui_.managerRestartDue(nowMs_)) {
        return false;
    }
    exitReason_ = "the manager update starts";
    updating_ = true;
    handLivenessToLauncher();
    return true;
}

void GuiSession::handLivenessToLauncher() {
    const core::Result<uint64_t> now = core::monotonicMs();
    if (!now.get() || !plugin_.handTo(getppid(), now.value())) {
        nbt_logf("liveness not handed to the launcher (errno %d)", errno);
    }
}

} // namespace gui
} // namespace nbt
