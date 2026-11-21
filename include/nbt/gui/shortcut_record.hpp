// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_SHORTCUT_RECORD_HPP
#define NBT_GUI_SHORTCUT_RECORD_HPP
#include "nbt/bridge_hold.h"
#include <stddef.h>
#include <stdint.h>
/* The App shortcut (plugin/NbtManager.xml, the second entry): iDrive's entry,
 * often stored on a favourite button, for the application chosen on its
 * settings page (manager/shortcut, SET manager shortcut). The plugin starts
 * `nbt-manager-launcher --shortcut`; its worker brings a running application
 * back (show) and writes nothing, or writes one line here for what needs the
 * manager window: "<ms> open" (no application chosen, or one that cannot come
 * back by itself) or "<ms> start <app>" (not running: the manager opens and
 * starts it, as if chosen in its list). The plugin then opens the manager; the
 * GUI starts <app> once and writes "<ms> taken". <ms> is CLOCK_MONOTONIC. A
 * private record of this user (phone_wake.hpp), plugin-safe (no heap, no STL). */
namespace nbt {
namespace gui {
static const char kShortcutRecordPath[] = "/dev/shmem/nbt-manager.shortcut";
/* kShortcutRecordPath (tests: NBT_TEST_SHORTCUT_RECORD). */
const char *shortcutRecordPath();
/* A record is acted on this long after it was written, no later. */
const uint64_t kShortcutFreshMs = 15000;
enum ShortcutWord { ShortcutOpen, ShortcutStart, ShortcutTaken };
struct ShortcutRecord {
    uint64_t ms;
    ShortcutWord word;
    char app[NBT_HOLD_APP_MAX + 1]; // ShortcutStart only, else ""
};
bool shortcutRecordParse(const char *text, size_t size, ShortcutRecord &record);
/* At most 80 bytes; false for an invalid record (an application name that is
 * not a token) or when it cannot be written. */
int shortcutRecordFormat(const ShortcutRecord &record, char *out, size_t size);
bool shortcutRecordWrite(const char *path, const ShortcutRecord &record);
bool shortcutRecordRead(const char *path, ShortcutRecord &record);
/* The record still to act on at `now`: written at or after `since`, fresh, of
 * the wanted word ("open" or "start" for the plugin; "start" for the GUI). */
bool shortcutRecordDue(const ShortcutRecord &record, uint64_t since, uint64_t now);
} // namespace gui
} // namespace nbt
#endif
