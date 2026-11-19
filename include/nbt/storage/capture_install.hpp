// SPDX-License-Identifier: MPL-2.0
/* Transactional installation, recovery, and safe removal of the capture shim. */
#ifndef NBT_STORAGE_CAPTURE_INSTALL_HPP
#define NBT_STORAGE_CAPTURE_INSTALL_HPP

#include "nbt/fs.h"
#include "nbt/manager_refresh.h"
#include "nbt/release.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 when done or already current (*changed set when anything changed),
 * NBT_CAPTURE_REFUSED when the release's shim failed its load or live test
 * (nothing changed, `error` says why), -1 on failure with `error` set.
 * Caller must hold the manager operation lock. Pass remount=NULL when /fs/sda0
 * is already remounted writable (e.g. launcher --refresh-setup). */
#define NBT_CAPTURE_REFUSED 2
int nbt_capture_refresh_from_release(nbt_fs *fs, const char *hmi_root, const char *starter_cfg_path,
                                     int release_fd, const nbt_release *release,
                                     nbt_remount_fn remount, int *changed, char *error,
                                     unsigned error_size);

/* Safely removes capture preload reference from starter.cfg first, verifies
 * read-back, then removes libnbt-capture.so and backups. Refuses if starter.cfg
 * has unrelated changes, retaining shim and backups. Caller holds operation lock. */
int nbt_capture_remove_installed(nbt_fs *fs, const char *hmi_root, const char *starter_cfg_path,
                                 nbt_remount_fn remount, char *error, unsigned error_size);

/* Returns 1 if the loaded capture shim acknowledges its identity in diag shm
 * (0 if absent/pending restart). Writes a short status string to `out`. */
int nbt_capture_operational_status(const char *diag_path, char *out, unsigned out_size);

#ifdef __cplusplus
}
#endif
#endif
