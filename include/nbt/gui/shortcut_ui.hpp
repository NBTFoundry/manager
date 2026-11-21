// SPDX-License-Identifier: MPL-2.0
/* An application's iDrive App shortcut (nbt/manager_settings.h): a row after
 * "Start with the car", read from SETTINGS <app> live (+shortcut<n>) and
 * set with SET manager shortcut<n> <app>|off. Selecting it moves the
 * application to the next entry, 1, 2, 3, then none; the page then says
 * where iDrive shows the entry. */
#ifndef NBT_GUI_SHORTCUT_UI_HPP
#define NBT_GUI_SHORTCUT_UI_HPP

#include "nbt/gui/ui_controller.hpp"

namespace nbt {
namespace gui {

/* Select on the row; false: not its key. */
bool shortcutPress(UiController &ui, Key key);
bool shortcutRequest(const UiController &ui, int kind, Request &request);
int shortcutReply(UiController &ui, const char *reply);
/* The row, "App shortcut: 2" or "App shortcut: off". */
void shortcutEntry(const UiController &ui, char *out, size_t size);
/* The page's note while the application has an entry, or false. */
bool shortcutNote(const UiController &ui, char *out, size_t size);
/* The manager opened for an App shortcut whose application does not run
 * (shortcut_record.hpp, "start <app>"): once the list shows, it is started as
 * if chosen there, once (the record then says "taken"). */
void shortcutStartPoll(UiController &ui, uint64_t nowMs);

} // namespace gui
} // namespace nbt

#endif
