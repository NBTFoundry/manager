// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_SHORTCUT_DISPATCH_HPP
#define NBT_WORKER_SHORTCUT_DISPATCH_HPP

#include "nbt/fs.h"

namespace nbt {
namespace worker {

/* iDrive's App shortcut `slot` (1 to NBT_SHORTCUTS) was pressed
 * (plugin/NbtManager.xml, nbt/manager_settings.h). The application assigned
 * to it comes back when it runs and can (show: SIGUSR1 through its host);
 * otherwise the plugin is told, in the shortcut record
 * (nbt/gui/shortcut_record.hpp), to open the manager: "start <app>" when it
 * does not run (the GUI starts it, as if chosen in its list), "open" when
 * none is assigned or it cannot come back by itself. 0, or 1 when nothing
 * could be done (the record not written). */
int shortcutDispatch(nbt_fs *fs, unsigned slot);

/* `nbt-managerd --shortcut<n>`: shortcutDispatch, as the launcher runs it
 * (launcher/door.cpp); in tests also `--root <dir>`. */
int shortcutMain(const char *root, const char *self, int argc, char **argv);

/* "--shortcut<n>" as a slot, or 0. */
unsigned shortcutSlot(const char *argument);

} // namespace worker
} // namespace nbt

#endif
