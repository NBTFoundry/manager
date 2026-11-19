#ifndef NBT_GUI_SETUP_UI_HPP
#define NBT_GUI_SETUP_UI_HPP
#include "nbt/gui/ui_controller.hpp"
namespace nbt {
namespace gui {
/* The manager's own installation (src/gui/setup_ui.cpp): SETUP once the list
 * shows, and the "Restart iDrive?" question when it replaced the iDrive plugin. */
static const uint64_t kSetupRetryMs = 12000; // past the 10 s health check of a trial
bool setupNow(const UiController &ui, uint64_t nowMs);
bool setupPress(UiController &ui, Key key);
bool setupRequest(int kind, Request &request);
int setupReply(UiController &ui, int kind, const char *reply, uint64_t now);
void setupView(UiView &view);
} // namespace gui
} // namespace nbt
#endif
