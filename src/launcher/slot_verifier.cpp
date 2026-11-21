// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/slot_verifier.hpp"

#include "nbt/launcher/slot_fs.hpp"
#include "nbt/launcher/stager.hpp"
#include "nbt/manager_store.h"
#include "nbt/model/installed_record.hpp"
#include "nbt/release.h"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace launcher {

int SlotVerifier::readProfileCompat(nbt_fs *fs, char out[49]) {
    char text[128];
    if (model::InstalledRecord::readText(fs, NBT_FS_MANAGER_PROFILE, "manager", text,
                                         sizeof(text)) ||
        strncmp(text, "NBTMANAGERPROFILE1\n", 19)) {
        return -1;
    }
    char *cursor = text + 19;
    char *value = nbt_field(&cursor, "compat");
    if (!nbt_token_valid(value, 48, NBT_TOKEN_DOT) || *cursor) {
        return -1;
    }
    memcpy(out, value, strlen(value) + 1);
    return 0;
}

int SlotVerifier::verifySlotWithKey(nbt_fs *fs, const nbt_manager_slot *record, int keyFd,
                                    const char *compat) {
    nbt_release active;
    char slot[2] = {static_cast<char>('0' + record->slot), 0};
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot, NBT_FS_READ);
    if (fd < 0) {
        return -1;
    }
    if (nbt_release_verify(fd, keyFd, compat, &active, NULL, 0)) {
        close(fd);
        return -1;
    }
    const int runner = nbt_release_find(&active, NBT_RELEASE_RUNNER_NAME);
    if (close(fd) || active.sequence != record->sequence ||
        !nbt_constant_equal(active.sha256, record->release_hash, 32) ||
        SlotFs::fileMatches(fs, NBT_FS_MANAGER_SLOT_GUI, slot, active.components[1].size,
                            active.components[1].sha256, true) ||
        SlotFs::fileMatches(fs, NBT_FS_MANAGER_SLOT_WORKER, slot, active.components[2].size,
                            active.components[2].sha256, true) ||
        (runner >= 0 &&
         SlotFs::fileMatches(fs, NBT_FS_MANAGER_SLOT_RUNNER, slot, active.components[runner].size,
                             active.components[runner].sha256, true))) {
        return -1;
    }
    return 0;
}

int SlotVerifier::preflightInitialSlot(nbt_fs *fs, const char *slot, const nbt_release *release,
                                       uint64_t size) {
    int present = nbt_fs_exists(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot);
    if (present < 0 || (present && SlotFs::fileMatches(fs, NBT_FS_MANAGER_SLOT_RELEASE, slot, size,
                                                       release->sha256, false))) {
        return -1;
    }
    present = nbt_fs_exists(fs, NBT_FS_MANAGER_SLOT_GUI, "manager", slot);
    if (present < 0 || (present && SlotFs::fileMatches(fs, NBT_FS_MANAGER_SLOT_GUI, slot,
                                                       release->components[1].size,
                                                       release->components[1].sha256, true))) {
        return -1;
    }
    present = nbt_fs_exists(fs, NBT_FS_MANAGER_SLOT_WORKER, "manager", slot);
    if (present < 0 || (present && SlotFs::fileMatches(fs, NBT_FS_MANAGER_SLOT_WORKER, slot,
                                                       release->components[2].size,
                                                       release->components[2].sha256, true))) {
        return -1;
    }
    const int runner = nbt_release_find(release, NBT_RELEASE_RUNNER_NAME);
    present = nbt_fs_exists(fs, NBT_FS_MANAGER_SLOT_RUNNER, "manager", slot);
    if (present < 0 || (present && (runner < 0 || SlotFs::fileMatches(
                                                      fs, NBT_FS_MANAGER_SLOT_RUNNER, slot,
                                                      release->components[runner].size,
                                                      release->components[runner].sha256, true)))) {
        return -1;
    }
    return 0;
}

int SlotVerifier::clearSlot(nbt_fs *fs, const char *slot, int initial) {
    // The programs first; an initial setup keeps them (preflightInitialSlot
    // checked them) and removes only the partial copies.
    const nbt_fs_path_kind kinds[] = {
        NBT_FS_MANAGER_SLOT_RELEASE,      NBT_FS_MANAGER_SLOT_GUI,
        NBT_FS_MANAGER_SLOT_WORKER,       NBT_FS_MANAGER_SLOT_RUNNER,
        NBT_FS_MANAGER_SLOT_RELEASE_NEXT, NBT_FS_MANAGER_SLOT_GUI_NEXT,
        NBT_FS_MANAGER_SLOT_WORKER_NEXT,  NBT_FS_MANAGER_SLOT_RUNNER_NEXT};
    const int kPrograms = 4, kKinds = 8;
    int exists[kKinds];
    int changed = 0;
    for (int i = 0; i < kKinds; ++i) {
        exists[i] = nbt_fs_exists(fs, kinds[i], "manager", slot);
        if (exists[i] < 0) {
            return -1;
        }
    }
    for (int i = 0; i < kKinds; ++i) {
        if (exists[i] && (!initial || i >= kPrograms)) {
            if (nbt_fs_unlink(fs, kinds[i], "manager", slot)) {
                return -1;
            }
            changed = 1;
            if (i == 0) {
                SlotFs::stageFault("clear-release");
            } else if (i == 1) {
                SlotFs::stageFault("clear-gui");
            } else if (i == 2) {
                SlotFs::stageFault("clear-worker");
            }
        }
    }
    return changed ? SlotFs::slotBarrier(fs, slot) : 0;
}

int SlotVerifier::checkBridgeDependencies(nbt_fs *fs, const nbt_bridge_provider *provider,
                                          int recovery, int locks[NBT_APP_LIST_MAX], char *warning,
                                          unsigned warningSize, char *error, unsigned errorSize) {
    char apps[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1];
    char reason[128];
    const int count = nbt_fs_list_apps(fs, apps);
    if (count < 0) {
        return SlotFs::fail(error, errorSize, "cannot inspect installed applications");
    }
    for (int i = 0; i < count; ++i) {
        nbt_installed record;
        locks[i] = nbt_tx_lock(fs, apps[i]);
        if (locks[i] < 0 || nbt_fs_exists(fs, NBT_FS_JOURNAL, apps[i], NULL) != 0 ||
            nbt_fs_exists(fs, NBT_FS_JOURNAL_NEXT, apps[i], NULL) != 0) {
            if (error && errorSize) {
                snprintf(error, errorSize, "%s: application update or recovery in progress",
                         apps[i]);
            }
            return -1;
        }
        if (nbt_tx_installed(fs, apps[i], &record)) {
            if (error && errorSize) {
                snprintf(error, errorSize, "%s: invalid installed record", apps[i]);
            }
            return -1;
        }
        // Not a refusal: the new release disables the application until it is
        // updated (app_compat.h), and staging names the first one.
        if (nbt_bridge_check(&record.bridge, provider, NULL, NULL, reason, sizeof(reason)) &&
            warning && warningSize && !*warning) {
            snprintf(warning, warningSize, "%s: %.40s: %.80s",
                     recovery ? "Recovery warning" : "Disabled until updated", apps[i], reason);
        }
    }
    return 0;
}

core::Status Stager::activeBridge(nbt_bridge_provider &provider) const {
    return nbt_manager_active_bridge(const_cast<nbt_fs *>(root_.legacy()), &provider)
               ? core::Status::fromErrno("manager bridge lookup failed")
               : core::Status();
}

core::Status Stager::verifySlot(const nbt_manager_slot &slot) const {
    return nbt_manager_verify_slot(const_cast<nbt_fs *>(root_.legacy()), &slot)
               ? core::Status::fromErrno("manager slot verification failed")
               : core::Status();
}

core::Status Stager::verifyRelease(int fd, const unsigned char expectedHash[32]) const {
    return nbt_manager_verify_release(const_cast<nbt_fs *>(root_.legacy()), fd, expectedHash)
               ? core::Status::fromErrno("manager release verification failed")
               : core::Status();
}

} // namespace launcher
} // namespace nbt

int nbt_manager_verify_slot(nbt_fs *fs, const nbt_manager_slot *slot) {
    char compat[49];
    if (!fs || !slot || slot->slot > 1 || !slot->sequence ||
        nbt::launcher::SlotVerifier::readProfileCompat(fs, compat)) {
        return -1;
    }
    const int key = nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ);
    if (key < 0) {
        return -1;
    }
    const int result = nbt::launcher::SlotVerifier::verifySlotWithKey(fs, slot, key, compat);
    return close(key) ? -1 : result;
}

int nbt_manager_active_bridge(nbt_fs *fs, nbt_bridge_provider *out) {
    nbt_manager_state state;
    nbt_release release;
    char compat[49];
    if (!out || nbt::launcher::SlotVerifier::readProfileCompat(fs, compat) ||
        nbt_manager_store_inspect(fs, &state) || state.pending) {
        return -1;
    }
    char slot[2] = {static_cast<char>('0' + state.active.slot), 0};
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot, NBT_FS_READ);
    const int key = nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ);
    int result = -1;
    if (fd >= 0 && key >= 0 && !nbt_release_verify(fd, key, compat, &release, NULL, 0) &&
        release.sequence == state.active.sequence &&
        nbt_constant_equal(release.sha256, state.active.release_hash, 32)) {
        *out = release.bridge;
        result = 0;
    }
    if (fd >= 0 && close(fd)) {
        result = -1;
    }
    if (key >= 0 && close(key)) {
        result = -1;
    }
    return result;
}

int nbt_manager_verify_release(nbt_fs *fs, int fd, const unsigned char expected_hash[32]) {
    char compat[49];
    nbt_release release;
    if (!fs || !expected_hash || nbt::launcher::SlotVerifier::readProfileCompat(fs, compat)) {
        return -1;
    }
    const int key = nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ);
    if (key < 0) {
        return -1;
    }
    const bool bad = nbt_release_verify(fd, key, compat, &release, NULL, 0) ||
                     !nbt_constant_equal(release.sha256, expected_hash, 32);
    return (close(key) || bad) ? -1 : 0;
}
