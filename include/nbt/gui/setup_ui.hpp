// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_SETUP_UI_HPP
#define NBT_GUI_SETUP_UI_HPP
#include "nbt/gui/ui_controller.hpp"
namespace nbt {
namespace gui {
/* The manager's own installation (src/gui/setup_ui.cpp): SETUP once the list
 * shows, and the "Restart iDrive?" question when it replaced the iDrive plugin. */
static const uint64_t kSetupRetryMs = 12000; // a busy worker
/* While an update is on trial the GUI asks again this often, so the restart
 * question follows the launcher's confirmation (NBT_HEALTHY_MS) at once. */
static const uint64_t kTrialPollMs = 1500;
bool setupNow(const UiController &ui, uint64_t nowMs);
bool setupPress(UiController &ui, Key key);
bool setupRequest(int kind, Request &request);
int setupReply(UiController &ui, int kind, const char *reply, uint64_t now);
void setupView(const UiController &ui, UiView &view);
} // namespace gui
} // namespace nbt
#endif
