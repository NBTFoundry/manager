#include "nbt/storage/path_policy.hpp"

#include "nbt/package.h"
#include "nbt/util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace storage {
namespace {

struct KindPath {
    PathKind kind;
    const char *path;
};

const KindPath kFixedPaths[] = {
    {NBT_FS_MANAGER_LOCK, "manager/gui.lock"},
    {NBT_FS_SESSION_UPLOAD, "state/manager/session.upload"},
    {NBT_FS_SURFACE_LOCK, "state/manager/surface.lock"},
    {NBT_FS_GAMEPAD_LOCK, "state/manager/gamepad.lock"},
    {NBT_FS_SETUP_APPS_DIR, "apps"},
    {NBT_FS_SETUP_DATA_DIR, "data"},
    {NBT_FS_SETUP_STATE_DIR, "state"},
    {NBT_FS_SETUP_MANAGER_STATE_DIR, "state/manager"},
    {NBT_FS_SETUP_TRUST_DIR, "trust"},
    {NBT_FS_SETUP_APP_PROFILES_DIR, "trust/apps"},
    {NBT_FS_SETUP_APP_KEYS_DIR, "trust/keys"},
    {NBT_FS_SETUP_MANAGER_DIR, "manager"},
    {NBT_FS_SETUP_SLOTS_DIR, "manager/slots"},
    {NBT_FS_SETUP_SLOT0_DIR, "manager/slots/0"},
    {NBT_FS_SETUP_SLOT1_DIR, "manager/slots/1"},
    {NBT_FS_SETUP_LAUNCHER, "manager/nbt-manager-launcher"},
    {NBT_FS_SETUP_KEY_NEXT, "trust/manager.pem.next"},
    {NBT_FS_SETUP_PROFILE_NEXT, "trust/manager.conf.next"},
    {NBT_FS_SETUP_LAUNCHER_NEXT, "manager/nbt-manager-launcher.next"},
};

const KindPath kManagerPaths[] = {
    {NBT_FS_DOOR_LOCK, "state/manager/door.lock"},
    {NBT_FS_PHONE_WAKE, "state/manager/phone.wake"},
    {NBT_FS_PHONE_UPLOAD, "state/manager/phone.upload"},
    {NBT_FS_PHONES_DIR, "trust/phones"},
    {NBT_FS_PHONES_LOCK, "state/manager/phones.lock"},
    {NBT_FS_PHONE_OPEN, "state/manager/phone-open"},
    {NBT_FS_PHONE_OPEN_NEXT, "state/manager/phone-open.next"},
    {NBT_FS_MANAGER_CONTROLLER_CONFIG, "manager/controller.conf"},
    {NBT_FS_MANAGER_CONTROLLER_CONFIG_NEXT, "manager/controller.conf.next"},
    {NBT_FS_MANAGER_LOG, "manager/debug.log"},
    {NBT_FS_MANAGER_LOG_OLD, "manager/debug.log.1"},
    {NBT_FS_MANAGER_CRASH, "manager/crash.gui"},
    {NBT_FS_MANAGER_OPERATION_LOCK, "state/manager/operation.lock"},
    {NBT_FS_MANAGER_STATE, "state/manager/activation"},
    {NBT_FS_MANAGER_STATE_NEXT, "state/manager/activation.next"},
    {NBT_FS_MANAGER_KEY, "trust/manager.pem"},
    {NBT_FS_MANAGER_PROFILE, "trust/manager.conf"},
    {NBT_FS_MANAGER_UPLOAD, "state/manager/release.upload"},
    {NBT_FS_MANAGER_UPLOAD_NEXT, "state/manager/release.upload.next"},
    {NBT_FS_MANAGER_STAGE_REQUEST, "state/manager/stage.request"},
    {NBT_FS_CAPTURE_STARTER_ORIG, "state/manager/starter.cfg.orig"},
    {NBT_FS_CAPTURE_STARTER_ORIG_NEXT, "state/manager/starter.cfg.orig.next"},
    {NBT_FS_CAPTURE_TX, "state/manager/capture.tx"},
    {NBT_FS_CAPTURE_TX_NEXT, "state/manager/capture.tx.next"},
    {NBT_FS_CAPTURE_PROBE, "state/manager/capture-probe.so"},
    {NBT_FS_HMI_ENV, "state/manager/hmi.env"},
    {NBT_FS_HMI_ENV_NEXT, "state/manager/hmi.env.next"},
    {NBT_FS_SHIM_STATE, "state/manager/shim-state"},
    {NBT_FS_SHIM_STATE_NEXT, "state/manager/shim-state.next"},
    {NBT_FS_CAPTURE_SHIM, "state/manager/libnbt-capture.so"},
    {NBT_FS_CAPTURE_SHIM_NEXT, "state/manager/libnbt-capture.so.next"},
};

const KindPath kAppStateNames[] = {
    {NBT_FS_UPLOAD, "upload"},
    {NBT_FS_JOURNAL, "journal"},
    {NBT_FS_JOURNAL_NEXT, "journal.next"},
    {NBT_FS_RECORD, "installed"},
    {NBT_FS_RECORD_NEXT, "installed.next"},
    {NBT_FS_LOCK, "lock"},
    {NBT_FS_LEASE, "lease"},
    {NBT_FS_SYNC, "sync"},
};

const KindPath kNonManagerStateNames[] = {
    {NBT_FS_HOST_LOCK, "host.lock"},
    {NBT_FS_SETTINGS_SCHEMA, "settings.schema"},
    {NBT_FS_SETTINGS_SCHEMA_NEXT, "settings.schema.next"},
    {NBT_FS_SETTINGS, "settings"},
    {NBT_FS_SETTINGS_NEXT, "settings.next"},
    {NBT_FS_AUTOSTART, "autostart"},
    {NBT_FS_AUTOSTART_NEXT, "autostart.next"},
    {NBT_FS_AUTOSTART_GUARD, "autostart-guard"},
    {NBT_FS_AUTOSTART_GUARD_NEXT, "autostart-guard.next"},
    {NBT_FS_FAULT_GUARD, "fault-guard"},
    {NBT_FS_FAULT_GUARD_NEXT, "fault-guard.next"},
    {NBT_FS_APP_LOG, "output.log"},
    {NBT_FS_APP_LOG_OLD, "output.log.1"},
};

const KindPath kSlotSuffixes[] = {
    {NBT_FS_MANAGER_SLOT_DIR, ""},
    {NBT_FS_MANAGER_SLOT_RELEASE, "/release.nbtr"},
    {NBT_FS_MANAGER_SLOT_GUI, "/gui"},
    {NBT_FS_MANAGER_SLOT_WORKER, "/worker"},
    {NBT_FS_MANAGER_SLOT_RELEASE_NEXT, "/release.nbtr.next"},
    {NBT_FS_MANAGER_SLOT_GUI_NEXT, "/gui.next"},
    {NBT_FS_MANAGER_SLOT_WORKER_NEXT, "/worker.next"},
};

template <size_t N> const char *lookupPath(const KindPath (&table)[N], PathKind kind) {
    for (size_t i = 0; i < N; ++i) {
        if (table[i].kind == kind) {
            return table[i].path;
        }
    }
    return NULL;
}

bool validTransaction(const char *tx) {
    unsigned char bytes[16];
    return tx && nbt_hex_decode(tx, bytes, sizeof(bytes)) == 0;
}

} // namespace

core::Status PathPolicy::relative(PathKind kind, const char *app, const char *tx,
                                  core::Span<char> output) {
    if (!nbt_token_valid(app, NBT_ID_MAX, 0)) {
        return core::Status::error(EPERM, "invalid application identifier");
    }
    if (!output.data() || output.size() > static_cast<size_t>(INT_MAX)) {
        return core::Status::error(EINVAL, "invalid path output");
    }
    char *out = output.data();
    const size_t cap = output.size();
    const bool isManager = strcmp(app, "manager") == 0;
    int count = -1;
    if (const char *fixed = lookupPath(kFixedPaths, kind)) {
        count = snprintf(out, cap, "%s", fixed);
    } else if (const char *mgr = lookupPath(kManagerPaths, kind)) {
        if (isManager) {
            count = snprintf(out, cap, "%s", mgr);
        }
    } else if (const char *stateName = lookupPath(kAppStateNames, kind)) {
        count = snprintf(out, cap, "state/%s/%s", app, stateName);
    } else if (const char *nonMgrState = lookupPath(kNonManagerStateNames, kind)) {
        if (!isManager) {
            count = snprintf(out, cap, "state/%s/%s", app, nonMgrState);
        }
    } else if (const char *slotSfx = lookupPath(kSlotSuffixes, kind)) {
        if (isManager && tx && (tx[0] == '0' || tx[0] == '1') && !tx[1]) {
            count = snprintf(out, cap, "manager/slots/%s%s", tx, slotSfx);
        }
    } else {
        switch (kind) {
        case NBT_FS_PHONE:
        case NBT_FS_PHONE_NEXT: {
            unsigned char keyId[32];
            if (isManager && tx && strlen(tx) == 64 && !nbt_hex_decode(tx, keyId, 32)) {
                bool lower = true;
                for (size_t i = 0; i < 64; ++i) {
                    lower =
                        lower && ((tx[i] >= '0' && tx[i] <= '9') || (tx[i] >= 'a' && tx[i] <= 'f'));
                }
                if (lower) {
                    count = snprintf(out, cap, "trust/phones/%s.conf%s", tx,
                                     kind == NBT_FS_PHONE_NEXT ? ".next" : "");
                }
            }
            break;
        }
        case NBT_FS_APP_DIR:
            count = snprintf(out, cap, "apps/%s/bin", app);
            break;
        case NBT_FS_STATE_DIR:
            count = snprintf(out, cap, "state/%s", app);
            break;
        case NBT_FS_TX_DIR:
            if (validTransaction(tx)) {
                count = snprintf(out, cap, "state/%s/tx-%s", app, tx);
            }
            break;
        case NBT_FS_ACTIVE:
            count = snprintf(out, cap, "apps/%s/bin/app", app);
            break;
        case NBT_FS_CANDIDATE:
            if (validTransaction(tx)) {
                count = snprintf(out, cap, "apps/%s/bin/.candidate-%s", app, tx);
            }
            break;
        case NBT_FS_BACKUP:
            if (validTransaction(tx)) {
                count = snprintf(out, cap, "state/%s/tx-%s/old", app, tx);
            }
            break;
        case NBT_FS_DATA_DIR:
        case NBT_FS_DATA_SETTINGS:
            if (!isManager) {
                count = snprintf(out, cap, "data/%s%s", app,
                                 kind == NBT_FS_DATA_SETTINGS ? "/settings" : "");
            }
            break;
        case NBT_FS_PROFILE:
            count = snprintf(out, cap, "trust/apps/%s.conf", app);
            break;
        case NBT_FS_KEY:
            if (tx && strlen(tx) == 1 && *tx >= '0' && *tx <= '7') {
                count = snprintf(out, cap, "trust/keys/%s-%s.pem", app, tx);
            }
            break;
        case NBT_FS_PROVISION_APP_ROOT:
            if (!isManager) {
                count = snprintf(out, cap, "apps/%s", app);
            }
            break;
        default:
            break;
        }
    }
    if (count < 0 || static_cast<size_t>(count) >= cap) {
        return core::Status::error(EINVAL, "path is not permitted or is too long");
    }
    return core::Status();
}

bool PathPolicy::canCreate(PathKind kind) {
    return kind == NBT_FS_PHONE_UPLOAD || kind == NBT_FS_PHONE_NEXT ||
           kind == NBT_FS_PHONE_OPEN_NEXT || kind == NBT_FS_UPLOAD || kind == NBT_FS_CANDIDATE ||
           kind == NBT_FS_JOURNAL_NEXT || kind == NBT_FS_RECORD_NEXT ||
           kind == NBT_FS_MANAGER_STATE_NEXT || kind == NBT_FS_MANAGER_UPLOAD ||
           kind == NBT_FS_MANAGER_UPLOAD_NEXT || kind == NBT_FS_MANAGER_SLOT_RELEASE ||
           kind == NBT_FS_MANAGER_SLOT_GUI || kind == NBT_FS_MANAGER_SLOT_WORKER ||
           kind == NBT_FS_MANAGER_SLOT_RELEASE_NEXT || kind == NBT_FS_MANAGER_SLOT_GUI_NEXT ||
           kind == NBT_FS_MANAGER_SLOT_WORKER_NEXT || kind == NBT_FS_MANAGER_LOG ||
           kind == NBT_FS_SESSION_UPLOAD || kind == NBT_FS_SETTINGS_SCHEMA_NEXT ||
           kind == NBT_FS_SETTINGS_NEXT || kind == NBT_FS_AUTOSTART_NEXT ||
           kind == NBT_FS_AUTOSTART_GUARD_NEXT || kind == NBT_FS_FAULT_GUARD_NEXT ||
           kind == NBT_FS_MANAGER_CONTROLLER_CONFIG_NEXT || kind == NBT_FS_MANAGER_STAGE_REQUEST ||
           kind == NBT_FS_CAPTURE_STARTER_ORIG_NEXT || kind == NBT_FS_CAPTURE_TX_NEXT ||
           kind == NBT_FS_CAPTURE_PROBE || kind == NBT_FS_APP_LOG || kind == NBT_FS_HMI_ENV_NEXT ||
           kind == NBT_FS_SHIM_STATE_NEXT || kind == NBT_FS_CAPTURE_SHIM_NEXT;
}

bool PathPolicy::canLock(PathKind kind) {
    return kind == NBT_FS_DOOR_LOCK || kind == NBT_FS_PHONES_LOCK || kind == NBT_FS_LOCK ||
           kind == NBT_FS_LEASE || kind == NBT_FS_HOST_LOCK || kind == NBT_FS_SYNC ||
           kind == NBT_FS_MANAGER_LOCK || kind == NBT_FS_MANAGER_OPERATION_LOCK ||
           kind == NBT_FS_SURFACE_LOCK || kind == NBT_FS_GAMEPAD_LOCK;
}

bool PathPolicy::canUnlink(PathKind kind) {
    return kind == NBT_FS_PHONE || kind == NBT_FS_JOURNAL || kind == NBT_FS_APP_LOG_OLD ||
           kind == NBT_FS_CAPTURE_STARTER_ORIG || kind == NBT_FS_CAPTURE_TX ||
           kind == NBT_FS_HMI_ENV || kind == NBT_FS_SHIM_STATE || kind == NBT_FS_CAPTURE_SHIM ||
           (canCreate(kind) && kind != NBT_FS_MANAGER_LOG);
}

bool PathPolicy::canReplace(PathKind from, PathKind to) {
    return (from == NBT_FS_PHONE_NEXT && to == NBT_FS_PHONE) ||
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
           (from == NBT_FS_SETTINGS_SCHEMA_NEXT && to == NBT_FS_SETTINGS_SCHEMA) ||
           (from == NBT_FS_SETTINGS_NEXT && to == NBT_FS_SETTINGS) ||
           (from == NBT_FS_AUTOSTART_NEXT && to == NBT_FS_AUTOSTART) ||
           (from == NBT_FS_AUTOSTART_GUARD_NEXT && to == NBT_FS_AUTOSTART_GUARD) ||
           (from == NBT_FS_FAULT_GUARD_NEXT && to == NBT_FS_FAULT_GUARD) ||
           (from == NBT_FS_MANAGER_CONTROLLER_CONFIG_NEXT &&
            to == NBT_FS_MANAGER_CONTROLLER_CONFIG);
}

bool PathPolicy::canRename(PathKind from, PathKind to, const char *app) {
    return canReplace(from, to) ||
           (from == NBT_FS_PHONE_UPLOAD && to == NBT_FS_SESSION_UPLOAD && app &&
            !strcmp(app, "manager")) ||
           (from == NBT_FS_MANAGER_SLOT_RELEASE_NEXT && to == NBT_FS_MANAGER_SLOT_RELEASE) ||
           (from == NBT_FS_MANAGER_SLOT_GUI_NEXT && to == NBT_FS_MANAGER_SLOT_GUI) ||
           (from == NBT_FS_MANAGER_SLOT_WORKER_NEXT && to == NBT_FS_MANAGER_SLOT_WORKER) ||
           (from == NBT_FS_ACTIVE && to == NBT_FS_BACKUP) ||
           (from == NBT_FS_SESSION_UPLOAD && to == NBT_FS_UPLOAD && app &&
            strcmp(app, "manager")) ||
           (from == NBT_FS_SESSION_UPLOAD && to == NBT_FS_MANAGER_UPLOAD && app &&
            !strcmp(app, "manager")) ||
           (from == NBT_FS_CANDIDATE && to == NBT_FS_ACTIVE);
}

} // namespace storage
} // namespace nbt
