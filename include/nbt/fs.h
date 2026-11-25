// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FS_H
#define NBT_FS_H

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#define NBT_PATH_MAX 1024
#define NBT_APP_LIST_MAX 16
#define NBT_APP_NAME_MAX 48

typedef struct {
    char root[NBT_PATH_MAX];
    dev_t device;
    ino_t inode;
    uid_t owner;
} nbt_fs;

typedef enum {
    NBT_FS_APP_DIR,
    NBT_FS_STATE_DIR,
    NBT_FS_TX_DIR,
    NBT_FS_ACTIVE,
    NBT_FS_CANDIDATE,
    NBT_FS_UPLOAD,
    NBT_FS_BACKUP,
    NBT_FS_JOURNAL,
    NBT_FS_JOURNAL_NEXT,
    NBT_FS_RECORD,
    NBT_FS_RECORD_NEXT,
    NBT_FS_LOCK,
    NBT_FS_LEASE,
    NBT_FS_SYNC,
    NBT_FS_PROFILE,
    NBT_FS_KEY,
    NBT_FS_MANAGER_LOCK,
    NBT_FS_MANAGER_OPERATION_LOCK,
    NBT_FS_MANAGER_STATE,
    NBT_FS_MANAGER_STATE_NEXT,
    NBT_FS_MANAGER_KEY,
    NBT_FS_MANAGER_PROFILE,
    NBT_FS_MANAGER_UPLOAD,
    NBT_FS_MANAGER_UPLOAD_NEXT,
    NBT_FS_MANAGER_SLOT_DIR,
    NBT_FS_MANAGER_SLOT_RELEASE,
    NBT_FS_MANAGER_SLOT_GUI,
    NBT_FS_MANAGER_SLOT_WORKER,
    NBT_FS_SETUP_APPS_DIR,
    NBT_FS_SETUP_DATA_DIR,
    NBT_FS_SETUP_STATE_DIR,
    NBT_FS_SETUP_MANAGER_STATE_DIR,
    NBT_FS_SETUP_TRUST_DIR,
    NBT_FS_SETUP_APP_PROFILES_DIR,
    NBT_FS_SETUP_APP_KEYS_DIR,
    NBT_FS_SETUP_MANAGER_DIR,
    NBT_FS_SETUP_SLOTS_DIR,
    NBT_FS_SETUP_SLOT0_DIR,
    NBT_FS_SETUP_SLOT1_DIR,
    NBT_FS_SETUP_LAUNCHER,
    NBT_FS_SETUP_KEY_NEXT,
    NBT_FS_SETUP_PROFILE_NEXT,
    NBT_FS_SETUP_LAUNCHER_NEXT,
    NBT_FS_MANAGER_SLOT_RELEASE_NEXT,
    NBT_FS_MANAGER_SLOT_GUI_NEXT,
    NBT_FS_MANAGER_SLOT_WORKER_NEXT,
    NBT_FS_MANAGER_CONTROLLER_CONFIG,
    NBT_FS_MANAGER_LOG,
    NBT_FS_MANAGER_LOG_OLD,
    NBT_FS_MANAGER_CRASH,
    NBT_FS_PROVISION_APP_ROOT,
    /* An upload received before its application is known (install or update
     * from the phone); renamed to that application's NBT_FS_UPLOAD. The path
     * does not depend on the application argument. */
    NBT_FS_SESSION_UPLOAD,
    /* Held by a running application's host (src/app_host.c). */
    NBT_FS_HOST_LOCK,
    /* The settings an application's installed package declares, and the
     * user's values (src/app_settings.c); each replaced through its .next. */
    NBT_FS_SETTINGS_SCHEMA,
    NBT_FS_SETTINGS_SCHEMA_NEXT,
    NBT_FS_SETTINGS,
    NBT_FS_SETTINGS_NEXT,
    /* manager/controller.conf.next: the controller configuration rewritten
     * from the manager's Settings (src/manager_settings.c). */
    NBT_FS_MANAGER_CONTROLLER_CONFIG_NEXT,
    NBT_FS_DATA_DIR,
    NBT_FS_DATA_SETTINGS,
    NBT_FS_AUTOSTART,
    NBT_FS_AUTOSTART_NEXT,
    NBT_FS_AUTOSTART_GUARD,
    NBT_FS_AUTOSTART_GUARD_NEXT,
    NBT_FS_FAULT_GUARD,
    NBT_FS_FAULT_GUARD_NEXT,
    /* Byte 0 is write-locked for life by the one application host whose
     * application has the manager's application surface (NBT_SURFACE). The
     * path does not depend on the application argument. */
    NBT_FS_SURFACE_LOCK,
    NBT_FS_GAMEPAD_LOCK, // USB ownership byte 0, GUI priority byte 1
    NBT_FS_PHONES_DIR,
    NBT_FS_PHONE,
    NBT_FS_PHONE_NEXT,
    NBT_FS_PHONES_LOCK,
    NBT_FS_PHONE_OPEN,
    NBT_FS_PHONE_OPEN_NEXT,
    NBT_FS_PHONE_UPLOAD,
    NBT_FS_PHONE_WAKE,
    NBT_FS_DOOR_LOCK,
    /* A manager release a paired phone sent and the driver approved: the
     * launcher stages state/manager/release.upload at the next opening. */
    NBT_FS_MANAGER_STAGE_REQUEST,
    NBT_FS_CAPTURE_STARTER_ORIG,
    NBT_FS_CAPTURE_STARTER_ORIG_NEXT,
    NBT_FS_CAPTURE_TX,
    NBT_FS_CAPTURE_TX_NEXT,
    /* An application's output and its host's notes on how it started and
     * ended (src/log/app_log.cpp), rotated to .1; not for the manager. */
    NBT_FS_APP_LOG,
    NBT_FS_APP_LOG_OLD,
    /* An application's service (capability `service`, docs/service-bridge.md):
     * its host's lock, its process's lease and its output, as host.lock, lease
     * and output.log are the application's. */
    NBT_FS_SERVICE_LOCK,
    NBT_FS_SERVICE_LEASE,
    NBT_FS_SERVICE_LOG,
    NBT_FS_SERVICE_LOG_OLD,
    /* A copy of the capture shim loaded once into a test process before
     * starter.cfg names the shim (src/storage/capture_probe.cpp). */
    NBT_FS_CAPTURE_PROBE,
    /* The environment file HmiMain3D's starter.cfg entry names: present, the
     * process manager starts iDrive with the shim (docs/hmi-restart.md). */
    NBT_FS_HMI_ENV,
    NBT_FS_HMI_ENV_NEXT,
    /* The shim's automatic state: armed, kept, failed or off, with its SHA-256. */
    NBT_FS_SHIM_STATE,
    NBT_FS_SHIM_STATE_NEXT,
    /* The capture shim itself, next to the environment file that names it. */
    NBT_FS_CAPTURE_SHIM,
    NBT_FS_CAPTURE_SHIM_NEXT,
    /* manager/language: the manager screen's language setting, replaced
     * through its .next (nbt/model/language.hpp). */
    NBT_FS_MANAGER_LANGUAGE,
    NBT_FS_MANAGER_LANGUAGE_NEXT,
    /* manager/shortcuts: the applications iDrive's App shortcut entries open
     * (nbt/manager_settings.h), replaced through its .next. */
    NBT_FS_MANAGER_SHORTCUTS,
    NBT_FS_MANAGER_SHORTCUTS_NEXT,
    /* manager/slots/<n>/runner: nbt-runner from the slot's release (NBTRLS3),
     * which runs applications built without the QNX SDP. */
    NBT_FS_MANAGER_SLOT_RUNNER,
    NBT_FS_MANAGER_SLOT_RUNNER_NEXT,
    /* manager/native-audio: the head unit's audio connection applications'
     * sound uses (nbt/manager_settings.h), replaced through its .next. */
    NBT_FS_MANAGER_NATIVE_AUDIO,
    NBT_FS_MANAGER_NATIVE_AUDIO_NEXT,
    /* state/<app>/crash.<n>: crash reports (nbt/log/crash_report.hpp), `tx`
     * the slot "0" to "4"; for the manager too (state/manager). Written as
     * crash.next, then renamed over the slot. */
    NBT_FS_CRASH_REPORT,
    NBT_FS_CRASH_REPORT_NEXT,
    /* trust/developers/<key id>.conf: the developer keys a paired phone added
     * and the driver approved (nbt/storage/developer_store.hpp), `tx` the key
     * id; replaced through its .next under the developers lock. */
    NBT_FS_DEVELOPERS_DIR,
    NBT_FS_DEVELOPER,
    NBT_FS_DEVELOPER_NEXT,
    NBT_FS_DEVELOPERS_LOCK,
    /* state/manager/diag.lock: held (flock) by the application host that is
     * using the car's diagnostic gateway (capability `diag`), one at a time. */
    NBT_FS_DIAG_LOCK
} nbt_fs_path_kind;

/* NBT_FS_APPEND opens an existing debug log (NBT_FS_MANAGER_LOG) for
 * appending, and an application log (NBT_FS_APP_LOG), creating it if needed. */
typedef enum {
    NBT_FS_READ,
    NBT_FS_CREATE,
    NBT_FS_OPEN_LOCK,
    NBT_FS_APPEND,
    NBT_FS_REWRITE
} nbt_fs_open_mode;

/* In production root is fixed by the caller; do not expose it to network input. */
int nbt_fs_init(nbt_fs *fs, const char *root, char *error, unsigned error_size);
int nbt_fs_path(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx, char *out,
                unsigned size);
int nbt_fs_open(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx,
                nbt_fs_open_mode mode);
/* Validated per-app directory; its contents belong to the app except the
 * reserved manager-written settings snapshot. Never created during OTA. */
int nbt_fs_app_data(nbt_fs *fs, const char *app, char out[NBT_PATH_MAX]);
/* NBT_FS_REWRITE is allowed only for NBT_FS_DATA_SETTINGS. It opens without
 * truncating. This bounded write keeps its inode, then truncates and syncs.
 * Readers must tolerate a partial/interrupted snapshot and retain defaults or
 * their last valid values. The state/<app>/settings file is authoritative. */
int nbt_fs_settings_write(nbt_fs *fs, const char *app, int fd, const char *text, size_t length);
/* Bounded durable text record replacement; only the explicitly paired settings
 * and autostart kinds are accepted. Caller serializes with the app tx lock. */
int nbt_fs_replace_text(nbt_fs *fs, nbt_fs_path_kind next, nbt_fs_path_kind target, const char *app,
                        const char *text, size_t size);
int nbt_fs_barrier(nbt_fs *fs, const char *app, const char *tx);
int nbt_fs_create_tx(nbt_fs *fs, const char *app, const char *tx);
int nbt_fs_rename(nbt_fs *fs, nbt_fs_path_kind from, nbt_fs_path_kind to, const char *app,
                  const char *tx);
int nbt_fs_unlink(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx);
int nbt_fs_exists(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx);
int nbt_fs_random_tx(char out[33]);
/* Explicit first-install operations, separate from ordinary update writes. */
FILE *nbt_fs_bootstrap_temp(void);
int nbt_fs_setup_root(nbt_fs *fs, const char *root, uint64_t required, char *error,
                      unsigned error_size);
int nbt_fs_setup_layout(nbt_fs *fs);
int nbt_fs_setup_create(nbt_fs *fs, nbt_fs_path_kind kind);
int nbt_fs_setup_publish(nbt_fs *fs, nbt_fs_path_kind next);
int nbt_fs_setup_discard(nbt_fs *fs, nbt_fs_path_kind next);
int nbt_fs_setup_sync(nbt_fs *fs);
/* Explicit provisioning of a new application by the local operator, separate
 * from package updates: creates apps/<app>, apps/<app>/bin, state/<app> and data/<app>
 * (all new; an existing one fails with EEXIST), then each of the executable,
 * record, profile and key "0" exactly once. Discard removes only these
 * entries (and the app's lock, lease and sync files) after a failed attempt. */
int nbt_fs_provision_dirs(nbt_fs *fs, const char *app);
int nbt_fs_provision_create(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx);
int nbt_fs_provision_discard(nbt_fs *fs, const char *app);
/* Makes the provisioned entries durable (the directories they were added to). */
int nbt_fs_provision_sync(nbt_fs *fs, const char *app);
/* 1 when any of an application's own paths exists (profile, key 0, apps/,
 * state/ or data/ directory), 0 when none does, -1 on error. */
int nbt_fs_app_present(nbt_fs *fs, const char *app);
/* Uninstall: removes everything of one application (never "manager"): its
 * profile first, so it leaves the application list, then keys 0-7, apps/<app>,
 * state/<app> and data/<app> with all they hold. Links are unlinked, never
 * followed, and nothing on another device is entered. Missing paths are not
 * errors, so a removal cut short completes when run again. The caller holds
 * the locks that keep the application stopped (lifecycle/app_remover.cpp). */
int nbt_fs_app_remove(nbt_fs *fs, const char *app);

/* The only setup operation outside the managed root is this fixed HMI plugin
 * directory. Indexes identify the five signed plugin roles, never paths. */
#define NBT_HMI_FILES 5

typedef struct {
    char root[NBT_PATH_MAX];
    dev_t device;
    ino_t inode;
    uid_t owner;
} nbt_hmi_fs;

int nbt_hmi_init(nbt_hmi_fs *hmi, const char *root);
/* As nbt_hmi_init; on failure `reason` names the check that failed. The root
 * must be a directory of the installing user on a writable qnx6 filesystem,
 * writable by nobody else except root's group (the car's folder is 0775). */
int nbt_hmi_check(nbt_hmi_fs *hmi, const char *root, const char **reason);
int nbt_hmi_space(nbt_hmi_fs *hmi, uint64_t required);
int nbt_hmi_entries(nbt_hmi_fs *hmi, int temporary);
int nbt_hmi_prepare(nbt_hmi_fs *hmi);
int nbt_hmi_open(nbt_hmi_fs *hmi, int temporary, unsigned index, int create);
int nbt_hmi_discard(nbt_hmi_fs *hmi, unsigned index);
int nbt_hmi_publish(nbt_hmi_fs *hmi);
int nbt_hmi_sync(nbt_hmi_fs *hmi, int temporary);
/* The file name of signed plugin role `index`, or NULL. */
const char *nbt_hmi_name(unsigned index);
/* Replacing a live plugin (hmi_plugin_swap.cpp): prepare the temporary folder
 * beside it, then swap the folders through `.NbtManager.old`, never writing a
 * file iDrive may have loaded. drop_old removes a leftover old folder. */
int nbt_hmi_prepare_replace(nbt_hmi_fs *hmi);
int nbt_hmi_replace(nbt_hmi_fs *hmi);
int nbt_hmi_drop_old(nbt_hmi_fs *hmi);

/* Ephemeral bridge status: only the fixed per-app shared-memory name, with
 * a root-local equivalent in host tests. create replaces a validated old file
 * with a fresh, private regular file before launching a new instance. */
int nbt_fs_status_open(nbt_fs *fs, const char *app, int create);
/* The same for another per-application bridge file, nbt-bridge1-<app>.<suffix>
 * (suffix "status" or "hold", capability `hold`), in nbt_fs_bridge_dir(). */
int nbt_fs_bridge_open(nbt_fs *fs, const char *app, const char *suffix, int create);
/* /dev/shmem; the managed root in host tests. */
const char *nbt_fs_bridge_dir(const nbt_fs *fs);
/* One in-place write plus truncate, no rename/fsync. The caller owns fd;
 * only a validated regular, singly-linked descriptor may be modified. */
int nbt_fs_status_write(int fd, const char *line, size_t length);

/* Exclusive lease is required before replacing or recovering an executable. */
int nbt_fs_lease_exclusive(nbt_fs *fs, const char *app);
/* The same for the application's service's lease (docs/service-bridge.md). */
int nbt_fs_service_lease_exclusive(nbt_fs *fs, const char *app);
/* Serialize manager slot staging, launch and recovery. Caller closes the fd. */
int nbt_fs_manager_operation_lock(nbt_fs *fs);
/* The launcher holds the manager operation lock for as long as the manager it
 * supervises runs (src/launcher/supervisor.cpp), which keeps manager updates
 * and setup out as surely as holding it here would. For the worker it started:
 * an unlocked descriptor of the lock file when the lock's holder is this
 * process's parent; otherwise -1 with errno EBUSY. Closing it releases nothing. */
int nbt_fs_manager_operation_parent(nbt_fs *fs);
int nbt_fs_list_apps(nbt_fs *fs, char names[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1]);

#endif
