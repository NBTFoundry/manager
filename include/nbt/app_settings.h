// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_SETTINGS_H
#define NBT_APP_SETTINGS_H

/* Stored application settings (see nbt/settings.h). The declaration of the
 * installed package is state/<app>/settings.schema, written when the
 * application is provisioned and when an installation is confirmed, so a
 * failed update keeps the previous one. The user's values are
 * state/<app>/settings. Both are replaced atomically through a .next file.
 * A bounded in-place data/<app>/settings snapshot is published afterwards. */

#include "nbt/bridge.h"
#include "nbt/fs.h"
#include "nbt/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The declaration with the user's values applied. An application without a
 * stored declaration has no settings (count 0). Unreadable values leave the
 * defaults. Returns -1 when the declaration cannot be read. */
int nbt_app_settings_load(nbt_fs *fs, const char *app, nbt_settings *settings);
int nbt_app_settings_save_schema(nbt_fs *fs, const char *app, const nbt_settings *schema);
/* Sets one declared choice under the app transaction lock. Callers must not
 * already hold that lock: POSIX locks belong to a process, not a descriptor.
 * A late snapshot failure may leave the durable choice saved; retry repairs it. */
int nbt_app_settings_set(nbt_fs *fs, const char *app, const char *key, const char *value);

/* The environment an application starts with: NBT_LAUNCH=<launch> ("user"
 * when started from the menu, "check" for the health check of an update),
 * NBT_BRIDGE and NBT_BRIDGE_CAPS only for bridge applications, then
 * NBT_SETTING_<KEY>=<value> for each setting, after the HMI's PATH and
 * runtime variables (nbt/process/hmi_environment.hpp). Hosts granted status also pass
 * NBT_STATUS_FD=5 for user launches; health checks have no status descriptor.
 * User launches granted most get NBT_MOST_FD=6 and gamepad grants get\n * NBT_GAMEPAD_FD=7; the
 * host adds NBT_SURFACE for `show` (nbt_app_surface_claim). Settings that cannot be read are left
 * out; the application then uses its own defaults. Every app gets NBT_DATA_DIR; live-settings
 * grants get NBT_SETTINGS_FILE, refreshed before launch; files grants get NBT_FILES_DIR when the
 * files root opens (storage/files_root.hpp), else nothing. An unsafe data directory or live
 * snapshot refuses launch. `available` lists the capabilities available now (NULL: all
 * implemented), as for nbt_bridge_check. */
#define NBT_APP_ENV_TEXT_MAX (16 + NBT_BRIDGE_CAPS_MAX + 1)
/* text is indexed like list, whose first entries may point elsewhere: the
 * HMI's runtime variables (at most 5), data dir, settings file, files dir, then
 * bridge (2), status, MOST, gamepad, controller, hold (2), diag, role, car
 * data, network, audio connection and input, launch and the settings: at most
 * NBT_SETTINGS_MAX + 23 entries. A host adds NBT_SURFACE in `surface`, then
 * the NULL. */
typedef struct {
    char text[NBT_SETTINGS_MAX + 23][NBT_APP_ENV_TEXT_MAX];
    char *list[NBT_SETTINGS_MAX + 25];
    char data_dir[NBT_PATH_MAX + 16];
    char settings_file[NBT_PATH_MAX + 20];
    char files_dir[NBT_PATH_MAX + 16];
    char surface[24];
    char library_path[512]; /* the HMI's LD_LIBRARY_PATH with /opt/mm/lib */
    char preload[512];      /* LD_PRELOAD with capture shim stripped */
} nbt_app_environment;

int nbt_app_environment_build(nbt_fs *fs, const char *app, const char *launch,
                              const nbt_bridge_requirement *bridge, const char *available,
                              nbt_app_environment *out);

#ifdef __cplusplus
}
#endif
#endif
