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
/* What the main menu tells in a dialog, closed with the push or BACK: the
 * message, else the notice, else a capture note not closed yet; NULL for none
 * (or while the manager update's own dialog shows). Starting and stopping show
 * on the application's row instead (view_builder.cpp). */
const char *menuNote(const UiController &ui);
/* What happens to application `i` (starting, stopping, running, only its
 * service running, needs an update) shows on its row, so the list never
 * moves; NULL for nothing. */
const char *rowNote(const UiController &ui, size_t i);
void closeMenuNote(UiController &ui);
} // namespace gui
} // namespace nbt
#endif
