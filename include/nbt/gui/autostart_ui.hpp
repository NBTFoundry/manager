// SPDX-License-Identifier: MPL-2.0
/* An application's "Start with the car" (PROTOCOL.md, "Autostart"): a
 * manager-provided row after the settings its package declares, read from
 * SETTINGS <app> live (+autostart, +autostart-paused) and switched with
 * AUTOSTART <app> on|off. Selecting it switches it at once. While automatic
 * starts are paused after failed ones, the page says how to resume them. */
#ifndef NBT_GUI_AUTOSTART_UI_HPP
#define NBT_GUI_AUTOSTART_UI_HPP

#include "nbt/gui/ui_controller.hpp"

namespace nbt {
namespace gui {

/* Select on the row; false: not its key. */
bool autostartPress(UiController &ui, Key key);
bool autostartRequest(const UiController &ui, int kind, Request &request);
int autostartReply(UiController &ui, const char *reply);
/* The row, "Start with the car: on". */
void autostartEntry(const UiController &ui, char *out, size_t size);
/* The page's note while automatic starts are paused, or false. */
bool autostartNote(const UiController &ui, char *out, size_t size);

} // namespace gui
} // namespace nbt

#endif
