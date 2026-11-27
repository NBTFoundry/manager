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
 * setting has at most six choices, and the GUI has its own Language screen.
 *
 * And shortcut1, shortcut2, shortcut3 <app>|off: which application iDrive's
 * entries App shortcut 1-3 open (plugin/NbtManager.xml), in manager/shortcuts
 * ("<app>\n" per entry, an empty line for none). An application takes one
 * entry at most: giving it another frees the first. Shown on each
 * application's own page (SETTINGS <app> live: +shortcut<n>).
 *
 *   native_audio  interrupt|media  the head unit's audio connection
 *                 applications' sound uses, in manager/native-audio
 *                 ("media\n"; absent or anything else is interrupt).
 *                 interrupt leaves applications on their own default, the
 *                 browser source 0xb8 (eSRC_BROWSER), an interrupting source
 *                 the car never turns below volume 25; media gives user
 *                 launches NBT_AUDIO_CONN=0x1d0 (eSRC_BON_BROWSER_ENT, the
 *                 media player's online stream source, an entertainment
 *                 source that follows the car's volume to 0) and
 *                 NBT_AUDIO_A2I=input_stereo_0, the io-amp input that source
 *                 plays (the SDK writes it through A2I). Both connections are
 *                 in the car's `audioconn -l`.
 *
 *   theme  auto|bmw|mini|supra  the colours of the manager's screen, in
 *          manager/theme ("mini\n"; absent or anything else is auto, the
 *          car's make: nbt/model/theme.hpp). */

/* What a user launch gets with native_audio media: the connection, and the
 * io-amp input its source plays (the media player's), which the SDK's audio
 * output then writes through A2I instead of the browser's libasound input. */
#define NBT_NATIVE_AUDIO_MEDIA_CONN 0x1d0u
#define NBT_NATIVE_AUDIO_MEDIA_INPUT "input_stereo_0"

#include "nbt/fs.h"
#include "nbt/package.h"
#include "nbt/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_SHORTCUTS 3

/* The application of each shortcut entry, "" for none (also when the file is
 * absent); -1 when it cannot be read. */
int nbt_manager_shortcuts_load(nbt_fs *fs, char apps[NBT_SHORTCUTS][NBT_ID_MAX + 1]);

/* 1 when native_audio is media, else 0 (also when unreadable). */
int nbt_manager_native_audio(nbt_fs *fs);

/* Returns -1 when the debug log's state cannot be read. */
int nbt_manager_settings_load(nbt_fs *fs, nbt_settings *settings);
/* Sets one of the settings above to one of its choices. */
int nbt_manager_settings_set(nbt_fs *fs, const char *key, const char *value);

#ifdef __cplusplus
}
#endif
#endif
