// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_TOOLS_UI_HPP
#define NBT_GUI_TOOLS_UI_HPP
#include "nbt/gui/ui_controller.hpp"
namespace nbt {
namespace gui {
/* Settings > Tools (src/gui/tools_ui.cpp): Restart head unit, which asks and
 * then sends the existing IDRIVE RESTART, and Enable SSH, which asks and sends
 * SSH ENABLE: sshd, telnetd and qconn as root until the head unit restarts,
 * the same as an SSH unlock, and nothing written (docs/system-tools.md). */
static const size_t kToolRestart = 0, kToolSsh = 1;
bool toolsPress(UiController &ui, Key key);
bool toolsRequest(const UiController &ui, int kind, Request &request);
int toolsReply(UiController &ui, int kind, const char *reply);
void toolsView(const UiController &ui, UiView &view);
} // namespace gui
} // namespace nbt
#endif
