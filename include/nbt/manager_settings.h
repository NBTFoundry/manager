// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MANAGER_SETTINGS_H
#define NBT_MANAGER_SETTINGS_H

/* The manager's own settings, opened from the main menu's Settings and
 * served by the worker as the settings of the reserved name "manager".
 * They are not stored as a declaration and values: each reflects, and
 * changes, the file the manager already uses.
 *
 *   debug_log  off|on  manager/debug.log exists (nbt/log.h). On creates it,
 *                      empty; off moves it to manager/debug.log.1, so the
 *                      last log can still be fetched, and logging stops.
 *   invert     off|on  invert= in manager/controller.conf
 *   release    off|on  release= in manager/controller.conf
 *
 * The controller values shown are those in effect: the defaults when the
 * file is absent or invalid. Changing one rewrites the whole file, through
 * controller.conf.next, with every key (other keys keep their values in
 * effect). Instances and key slots are left to the desktop installer: with
 * wrong ones the controller cannot reach this menu anyway.
 *
 * SET also takes language, auto|en|fr|de|it|es|zh|ja, written to
 * manager/language (nbt/model/language.hpp). It is not listed: a declared
 * setting has at most six choices, and the GUI has its own Language screen. */

#include "nbt/fs.h"
#include "nbt/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns -1 when the debug log's state cannot be read. */
int nbt_manager_settings_load(nbt_fs *fs, nbt_settings *settings);
/* Sets one of the settings above to one of its choices. */
int nbt_manager_settings_set(nbt_fs *fs, const char *key, const char *value);

#ifdef __cplusplus
}
#endif
#endif
