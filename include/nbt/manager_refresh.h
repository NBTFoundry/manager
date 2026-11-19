#ifndef NBT_MANAGER_REFRESH_H
#define NBT_MANAGER_REFRESH_H
#include "nbt/fs.h"
#include <sys/types.h>

/* An installed manager's stable launcher and iDrive plugin follow its release
 * (src/launcher/setup_refresh.cpp). First setup writes both once; a slot update
 * replaces only the GUI and the worker. The running worker calls this when the
 * GUI asks (SETUP), with the release of the active, confirmed slot. */
#define NBT_REFRESH_LAUNCHER 1u
#define NBT_REFRESH_PLUGIN 2u
#define NBT_REFRESH_CAPTURE 4u
/* The release's capture shim failed its load or live test: nothing of the
 * shim changed, the rest of the refresh went on (`error` says why). */
#define NBT_REFRESH_CAPTURE_REFUSED 8u

/* Makes the iDrive plugin partition writable (1) or read-only again (0):
 * 0 on success. */
typedef int (*nbt_remount_fn)(int writable);

/* 0 with `changed` set (0 when both were current), 1 while a manager update is
 * on trial (nothing done: the plugin follows the confirmed release only), -1
 * with `error` set. The partition is made read-only again on every path that
 * made it writable. */
int nbt_manager_refresh_installed(nbt_fs *fs, const char *hmi_root, nbt_remount_fn remount,
                                  unsigned *changed, char *error, unsigned error_size);

/* The installer's Update (`launcher --refresh-setup`, run from the new release
 * right after --stage-update, with the partition already writable): the same,
 * from the active release even while it is pending, so the iDrive plugin is
 * replaced without waiting for an opening. Needs the operation lock free (the
 * manager closed). 0 with `changed` set, -1 with `error` set. */
int nbt_manager_refresh_staged(nbt_fs *fs, const char *hmi_root, unsigned *changed, char *error,
                               unsigned error_size);

/* 1 when the stable launcher is the active release's (so it stages a phone
 * update at the next opening), 0 when it is older, -1 when the active release
 * cannot be verified. `version` gets the active release's version. */
int nbt_manager_launcher_current(nbt_fs *fs, char version[49]);

/* 1 when the installed stable launcher's `--capabilities` advertises `format`
 * (e.g. "NBTRLS2"), 0 when older/unsupported or missing, -1 on internal error. */
int nbt_manager_launcher_supports_format(nbt_fs *fs, const char *format);

/* The head unit's own tools (src/worker/system_tools.cpp): `mount -uw|-ur
 * /fs/sda0`, and `OnOffDSICommander appreset`, which restarts iDrive and this
 * manager with it. Found on the PATH iDrive passes on; 0 on success. */
int nbt_system_remount(int writable);
int nbt_system_restart_idrive(void);
/* Starts `sloginfo`, the iDrive processor's system log, writing to `out_fd`;
 * the caller waits for *pid. 0 on success. Host tests run the program named
 * by NBT_TEST_SLOGINFO instead. */
int nbt_system_log_start(int out_fd, pid_t *pid);

#endif
