// SPDX-License-Identifier: MPL-2.0
// What PathPolicy lets the managed root do with each kind of path.
#include "nbt/storage/path_policy.hpp"

#include <string.h>

namespace nbt {
namespace storage {

bool PathPolicy::canCreate(PathKind kind) {
    return kind == NBT_FS_PHONE_UPLOAD || kind == NBT_FS_PHONE_NEXT ||
           kind == NBT_FS_DEVELOPER_NEXT || kind == NBT_FS_PHONE_OPEN_NEXT ||
           kind == NBT_FS_UPLOAD || kind == NBT_FS_CANDIDATE || kind == NBT_FS_JOURNAL_NEXT ||
           kind == NBT_FS_RECORD_NEXT || kind == NBT_FS_MANAGER_STATE_NEXT ||
           kind == NBT_FS_MANAGER_UPLOAD || kind == NBT_FS_MANAGER_UPLOAD_NEXT ||
           kind == NBT_FS_MANAGER_SLOT_RELEASE || kind == NBT_FS_MANAGER_SLOT_GUI ||
           kind == NBT_FS_MANAGER_SLOT_WORKER || kind == NBT_FS_MANAGER_SLOT_RELEASE_NEXT ||
           kind == NBT_FS_MANAGER_SLOT_GUI_NEXT || kind == NBT_FS_MANAGER_SLOT_WORKER_NEXT ||
           kind == NBT_FS_MANAGER_SLOT_RUNNER || kind == NBT_FS_MANAGER_SLOT_RUNNER_NEXT ||
           kind == NBT_FS_MANAGER_LOG || kind == NBT_FS_SESSION_UPLOAD ||
           kind == NBT_FS_SETTINGS_SCHEMA_NEXT || kind == NBT_FS_SETTINGS_NEXT ||
           kind == NBT_FS_AUTOSTART_NEXT || kind == NBT_FS_AUTOSTART_GUARD_NEXT ||
           kind == NBT_FS_FAULT_GUARD_NEXT || kind == NBT_FS_MANAGER_CONTROLLER_CONFIG_NEXT ||
           kind == NBT_FS_MANAGER_STAGE_REQUEST || kind == NBT_FS_CAPTURE_STARTER_ORIG_NEXT ||
           kind == NBT_FS_CAPTURE_TX_NEXT || kind == NBT_FS_CAPTURE_PROBE ||
           kind == NBT_FS_APP_LOG || kind == NBT_FS_SERVICE_LOG || kind == NBT_FS_HMI_ENV_NEXT ||
           kind == NBT_FS_SHIM_STATE_NEXT || kind == NBT_FS_CAPTURE_SHIM_NEXT ||
           kind == NBT_FS_MANAGER_LANGUAGE_NEXT || kind == NBT_FS_MANAGER_SHORTCUTS_NEXT ||
           kind == NBT_FS_MANAGER_NATIVE_AUDIO_NEXT || kind == NBT_FS_CRASH_REPORT_NEXT;
}

bool PathPolicy::canLock(PathKind kind) {
    return kind == NBT_FS_DOOR_LOCK || kind == NBT_FS_PHONES_LOCK ||
           kind == NBT_FS_DEVELOPERS_LOCK || kind == NBT_FS_DIAG_LOCK || kind == NBT_FS_LOCK ||
           kind == NBT_FS_LEASE || kind == NBT_FS_HOST_LOCK || kind == NBT_FS_SYNC ||
           kind == NBT_FS_SERVICE_LOCK || kind == NBT_FS_SERVICE_LEASE ||
           kind == NBT_FS_MANAGER_LOCK || kind == NBT_FS_MANAGER_OPERATION_LOCK ||
           kind == NBT_FS_SURFACE_LOCK || kind == NBT_FS_GAMEPAD_LOCK;
}

bool PathPolicy::canUnlink(PathKind kind) {
    return kind == NBT_FS_PHONE || kind == NBT_FS_DEVELOPER || kind == NBT_FS_JOURNAL ||
           kind == NBT_FS_APP_LOG_OLD || kind == NBT_FS_SERVICE_LOG_OLD ||
           kind == NBT_FS_CAPTURE_STARTER_ORIG || kind == NBT_FS_CAPTURE_TX ||
           kind == NBT_FS_HMI_ENV || kind == NBT_FS_SHIM_STATE || kind == NBT_FS_CAPTURE_SHIM ||
           kind == NBT_FS_CRASH_REPORT || (canCreate(kind) && kind != NBT_FS_MANAGER_LOG);
}

bool PathPolicy::canReplace(PathKind from, PathKind to) {
    return (from == NBT_FS_PHONE_NEXT && to == NBT_FS_PHONE) ||
           (from == NBT_FS_DEVELOPER_NEXT && to == NBT_FS_DEVELOPER) ||
           (from == NBT_FS_PHONE_OPEN_NEXT && to == NBT_FS_PHONE_OPEN) ||
           (from == NBT_FS_BACKUP && to == NBT_FS_ACTIVE) ||
           (from == NBT_FS_JOURNAL_NEXT && to == NBT_FS_JOURNAL) ||
           (from == NBT_FS_RECORD_NEXT && to == NBT_FS_RECORD) ||
           (from == NBT_FS_MANAGER_STATE_NEXT && to == NBT_FS_MANAGER_STATE) ||
           (from == NBT_FS_MANAGER_UPLOAD_NEXT && to == NBT_FS_MANAGER_UPLOAD) ||
           (from == NBT_FS_CAPTURE_STARTER_ORIG_NEXT && to == NBT_FS_CAPTURE_STARTER_ORIG) ||
           (from == NBT_FS_CAPTURE_TX_NEXT && to == NBT_FS_CAPTURE_TX) ||
           (from == NBT_FS_HMI_ENV_NEXT && to == NBT_FS_HMI_ENV) ||
           (from == NBT_FS_SHIM_STATE_NEXT && to == NBT_FS_SHIM_STATE) ||
           (from == NBT_FS_CAPTURE_SHIM_NEXT && to == NBT_FS_CAPTURE_SHIM) ||
           (from == NBT_FS_SETUP_LAUNCHER_NEXT && to == NBT_FS_SETUP_LAUNCHER) ||
           (from == NBT_FS_MANAGER_LOG && to == NBT_FS_MANAGER_LOG_OLD) ||
           (from == NBT_FS_APP_LOG && to == NBT_FS_APP_LOG_OLD) ||
           (from == NBT_FS_SERVICE_LOG && to == NBT_FS_SERVICE_LOG_OLD) ||
           (from == NBT_FS_SETTINGS_SCHEMA_NEXT && to == NBT_FS_SETTINGS_SCHEMA) ||
           (from == NBT_FS_SETTINGS_NEXT && to == NBT_FS_SETTINGS) ||
           (from == NBT_FS_AUTOSTART_NEXT && to == NBT_FS_AUTOSTART) ||
           (from == NBT_FS_AUTOSTART_GUARD_NEXT && to == NBT_FS_AUTOSTART_GUARD) ||
           (from == NBT_FS_FAULT_GUARD_NEXT && to == NBT_FS_FAULT_GUARD) ||
           (from == NBT_FS_MANAGER_CONTROLLER_CONFIG_NEXT &&
            to == NBT_FS_MANAGER_CONTROLLER_CONFIG) ||
           (from == NBT_FS_MANAGER_LANGUAGE_NEXT && to == NBT_FS_MANAGER_LANGUAGE) ||
           (from == NBT_FS_MANAGER_SHORTCUTS_NEXT && to == NBT_FS_MANAGER_SHORTCUTS) ||
           (from == NBT_FS_MANAGER_NATIVE_AUDIO_NEXT && to == NBT_FS_MANAGER_NATIVE_AUDIO) ||
           (from == NBT_FS_CRASH_REPORT_NEXT && to == NBT_FS_CRASH_REPORT);
}

bool PathPolicy::canRename(PathKind from, PathKind to, const char *app) {
    return canReplace(from, to) ||
           (from == NBT_FS_PHONE_UPLOAD && to == NBT_FS_SESSION_UPLOAD && app &&
            !strcmp(app, "manager")) ||
           (from == NBT_FS_MANAGER_SLOT_RELEASE_NEXT && to == NBT_FS_MANAGER_SLOT_RELEASE) ||
           (from == NBT_FS_MANAGER_SLOT_GUI_NEXT && to == NBT_FS_MANAGER_SLOT_GUI) ||
           (from == NBT_FS_MANAGER_SLOT_WORKER_NEXT && to == NBT_FS_MANAGER_SLOT_WORKER) ||
           (from == NBT_FS_MANAGER_SLOT_RUNNER_NEXT && to == NBT_FS_MANAGER_SLOT_RUNNER) ||
           (from == NBT_FS_ACTIVE && to == NBT_FS_BACKUP) ||
           (from == NBT_FS_SESSION_UPLOAD && to == NBT_FS_UPLOAD && app &&
            strcmp(app, "manager")) ||
           (from == NBT_FS_SESSION_UPLOAD && to == NBT_FS_MANAGER_UPLOAD && app &&
            !strcmp(app, "manager")) ||
           (from == NBT_FS_CANDIDATE && to == NBT_FS_ACTIVE);
}

} // namespace storage
} // namespace nbt
