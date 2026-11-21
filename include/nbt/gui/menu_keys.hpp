// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_MENU_KEYS_HPP
#define NBT_GUI_MENU_KEYS_HPP
#include "nbt/gui/ui_controller.hpp"
#include <stdint.h>
namespace nbt {
namespace gui {
/* The main menu's held push: once held for kHoldMs, a running application is
 * stopped (STOP); one that does not run gets nothing, and the release then
 * starts nothing either. Called on each request turn. */
void advanceHold(UiController &ui, uint64_t nowMs);
/* OPTION: on the main menu, the focused application's settings (the manager's
 * on Settings); 1 when a request was queued, 0 elsewhere or while busy. */
int optionKey(UiController &ui);
} // namespace gui
} // namespace nbt
#endif
