#ifndef NBT_GUI_PHONE_UI_HPP
#define NBT_GUI_PHONE_UI_HPP
#include "nbt/gui/ui_controller.hpp"
namespace nbt {
namespace gui {
bool phonePress(UiController &ui, Key key);
bool phoneRequest(const UiController &ui, int kind, Request &request);
int phoneReply(UiController &ui, int kind, const char *reply, uint64_t now);
void phoneView(const UiController &ui, UiView &view);
} // namespace gui
} // namespace nbt
#endif
