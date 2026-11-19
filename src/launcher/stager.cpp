#include "nbt/launcher/stager.hpp"

#include "nbt/launcher/slot_fs.hpp"
#include "nbt/launcher/slot_verifier.hpp"
#include "nbt/log.h"
#include "nbt/manager_store.h"
#include "nbt/release.h"
#include "nbt/storage/path_open.hpp"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using nbt::launcher::SlotFs;
using nbt::launcher::SlotVerifier;

int copyRelease(nbt_fs *fs, const char *slot, int input, uint64_t size) {
    const int present = nbt_fs_exists(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot);
    if (present != 0) {
        return present > 0 ? 0 : -1;
    }
    const int output =
        nbt_fs_open(fs, NBT_FS_MANAGER_SLOT_RELEASE_NEXT, "manager", slot, NBT_FS_CREATE);
    if (output < 0) {
        return -1;
    }
    SlotFs::stageFault("release-created");
    if (SlotFs::copyStream(input, output, size, true)) {
        close(output);
        return -1;
    }
    SlotFs::stageFault("release-written");
    if (fsync(output)) {
        const int saved = errno;
        close(output);
        errno = saved;
        return -1;
    }
    SlotFs::stageFault("release-synced");
    if (close(output) ||
        nbt_fs_rename(fs, NBT_FS_MANAGER_SLOT_RELEASE_NEXT, NBT_FS_MANAGER_SLOT_RELEASE, "manager",
                      slot) ||
        SlotFs::slotBarrier(fs, slot)) {
        return -1;
    }
    return 0;
}

int copyComponent(nbt_fs *fs, const char *slot, int releaseFd, const nbt_release *release,
                  unsigned index, nbt_fs_path_kind kind) {
    const int present = nbt_fs_exists(fs, kind, "manager", slot);
    if (present != 0) {
        return present > 0 ? 0 : -1;
    }
    const nbt_fs_path_kind next =
        index == 1 ? NBT_FS_MANAGER_SLOT_GUI_NEXT : NBT_FS_MANAGER_SLOT_WORKER_NEXT;
    const int output = nbt_fs_open(fs, next, "manager", slot, NBT_FS_CREATE);
    if (output < 0) {
        return -1;
    }
    SlotFs::stageFault(index == 1 ? "gui-created" : "worker-created");
    int result = nbt_release_copy_component(releaseFd, release, index, output, NULL, 0);
    if (!result && fchmod(output, 0700)) {
        result = -1;
    }
    SlotFs::stageFault(index == 1 ? "gui-written" : "worker-written");
    if (!result &&
        (SlotFs::injectedStageError(index == 1 ? "gui-sync" : "worker-sync") || fsync(output))) {
        result = -1;
    }
    SlotFs::stageFault(index == 1 ? "gui-synced" : "worker-synced");
    if (close(output)) {
        result = -1;
    }
    if (!result &&
        (nbt_fs_rename(fs, next, kind, "manager", slot) || SlotFs::slotBarrier(fs, slot))) {
        result = -1;
    }
    return result;
}

int stageSlotFiles(nbt_fs *fs, const char *slot, int initial, int recovery, int input, int key,
                   const char *compat, uint64_t relSize, const nbt_release &release, char *error,
                   unsigned errorSize) {
    nbt_release copied;
    char verifyError[160];
    const uint64_t need = relSize + release.components[1].size + release.components[2].size +
                          (release.component_count > NBT_RELEASE_CAPTURE_INDEX
                               ? release.components[NBT_RELEASE_CAPTURE_INDEX].size
                               : 0);
    if (SlotFs::requireSpace(fs->root, need, 4)) {
        return SlotFs::fail(error, errorSize, "insufficient staging space");
    }
    if (initial && SlotVerifier::preflightInitialSlot(fs, slot, &release, relSize)) {
        return SlotFs::fail(error, errorSize, "conflicting initial manager slot");
    }
    if (SlotVerifier::clearSlot(fs, slot, initial)) {
        return SlotFs::fail(error, errorSize, "unsafe or occupied manager slot");
    }
    if (copyRelease(fs, slot, input, relSize)) {
        return SlotFs::fail(error, errorSize, "cannot stage release");
    }
    SlotFs::stageFault("release");
    const int staged = nbt_fs_open(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot, NBT_FS_READ);
    if (staged < 0 ||
        nbt_release_verify(staged, key, compat, &copied, verifyError, sizeof(verifyError)) ||
        !nbt_constant_equal(copied.sha256, release.sha256, 32)) {
        if (staged >= 0) {
            close(staged);
        }
        return SlotFs::fail(error, errorSize, "staged release changed");
    }
    if (copyComponent(fs, slot, staged, &copied, 1, NBT_FS_MANAGER_SLOT_GUI)) {
        close(staged);
        return SlotFs::fail(error, errorSize, "cannot stage manager GUI");
    }
    SlotFs::stageFault("gui");
    if (copyComponent(fs, slot, staged, &copied, 2, NBT_FS_MANAGER_SLOT_WORKER)) {
        close(staged);
        return SlotFs::fail(error, errorSize, "cannot stage manager worker");
    }
    close(staged);
    SlotFs::stageFault("worker");
    if (SlotFs::slotBarrier(fs, slot)) {
        return SlotFs::fail(error, errorSize, "cannot sync manager slot");
    }
    SlotFs::stageFault("slot-synced");
    if (initial ? nbt_manager_store_initial(fs, &copied)
                : nbt_manager_store_begin(fs, &copied, recovery)) {
        return SlotFs::fail(error, errorSize, "cannot persist manager activation");
    }
    SlotFs::stageFault("record");
    return 0;
}

} // namespace

int nbt_manager_discard_candidate(nbt_fs *fs) {
    nbt_manager_state state;
    if (!fs || nbt_manager_store_load(fs, &state)) {
        return -1;
    }
    if (!state.pending) {
        return 0;
    }
    if (nbt_manager_verify_slot(fs, &state.previous) || nbt_manager_store_rollback(fs)) {
        return -1;
    }
    nbt_logf("unconfirmed manager release %llu dropped; slot %u restored",
             static_cast<unsigned long long>(state.active.sequence), state.previous.slot);
    return 1;
}

int nbt_manager_stage(nbt_fs *fs, int initial, int recovery, const unsigned char expected_hash[32],
                      char *error, unsigned error_size) {
    nbt_manager_state state, candidate;
    nbt_release release;
    char compat[49], slot[2] = {'0', 0}, verifyError[160], warning[160] = "";
    struct stat st;
    int appLocks[NBT_APP_LIST_MAX];
    for (int i = 0; i < NBT_APP_LIST_MAX; ++i) {
        appLocks[i] = -1;
    }
    if (!fs || !expected_hash || (initial != 0 && initial != 1) ||
        (recovery != 0 && recovery != 1) || (initial && recovery)) {
        return SlotFs::fail(error, error_size, "invalid staging request");
    }
    const int operation = nbt_fs_manager_operation_lock(fs);
    if (operation < 0) {
        return SlotFs::fail(error, error_size, "manager operation is in progress");
    }
    const int lock =
        nbt::storage::SafePathOpen::lockNonBlocking(fs, NBT_FS_MANAGER_LOCK, "manager");
    if (lock < 0) {
        close(operation);
        return SlotFs::fail(error, error_size, "manager is running or locked");
    }
    int key = -1, input = -1, result = -1;
    if (SlotVerifier::readProfileCompat(fs, compat)) {
        SlotFs::fail(error, error_size, "invalid manager profile");
        goto done;
    }
    key = nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ);
    input = nbt_fs_open(fs, NBT_FS_MANAGER_UPLOAD, "manager", NULL, NBT_FS_READ);
    if (key < 0 || input < 0 || fstat(input, &st) || st.st_size < 0 ||
        nbt_release_verify(input, key, compat, &release, verifyError, sizeof(verifyError)) ||
        !nbt_constant_equal(release.sha256, expected_hash, 32)) {
        SlotFs::fail(error, error_size, "manager release verification failed");
        goto done;
    }
    if (SlotVerifier::checkBridgeDependencies(fs, &release.bridge, recovery, appLocks, warning,
                                              sizeof(warning), error, error_size)) {
        goto done;
    }
    if (initial) {
        const int loadResult = nbt_manager_store_load(fs, &state);
        const int loadError = errno;
        if (!loadResult || (loadError != ENOENT && loadError != EBUSY)) {
            SlotFs::fail(error, error_size, "manager is already installed or recovery is required");
            goto done;
        }
        if (!nbt_manager_state_initial(&candidate, &release) &&
            !SlotVerifier::verifySlotWithKey(fs, &candidate.active, key, compat)) {
            if (SlotFs::slotBarrier(fs, "0") || nbt_manager_store_resume_initial(fs, &release)) {
                SlotFs::fail(error, error_size, "cannot recover initial manager activation");
                goto done;
            }
            if (error && error_size) {
                *error = 0;
            }
            result = 0;
            goto done;
        }
        if (loadError == EBUSY) {
            SlotFs::fail(error, error_size, "initial manager slot requires manual recovery");
            goto done;
        }
    } else {
        if (nbt_manager_discard_candidate(fs) < 0 || nbt_manager_store_load(fs, &state) ||
            state.pending) {
            SlotFs::fail(error, error_size, "manager state requires recovery");
            goto done;
        }
        if (SlotVerifier::verifySlotWithKey(fs, &state.active, key, compat)) {
            SlotFs::fail(error, error_size, "current manager slot failed verification");
            goto done;
        }
        candidate = state;
        if (nbt_manager_state_begin(&candidate, &release, recovery)) {
            SlotFs::fail(error, error_size, "manager release sequence rejected");
            goto done;
        }
        slot[0] = static_cast<char>('0' + candidate.active.slot);
    }
    if (!stageSlotFiles(fs, slot, initial, recovery, input, key, compat,
                        static_cast<uint64_t>(st.st_size), release, error, error_size)) {
        if (error && error_size) {
            snprintf(error, error_size, "%s", warning);
        }
        result = 0;
    }
done:
    for (int i = 0; i < NBT_APP_LIST_MAX; ++i) {
        if (appLocks[i] >= 0) {
            close(appLocks[i]);
        }
    }
    if (input >= 0) {
        close(input);
    }
    if (key >= 0) {
        close(key);
    }
    close(lock);
    close(operation);
    return result;
}

namespace nbt {
namespace launcher {

core::Status Stager::stage(bool initial, bool recovery, const unsigned char expectedHash[32],
                           char *error, unsigned errorSize) const {
    return nbt_manager_stage(const_cast<nbt_fs *>(root_.legacy()), initial, recovery, expectedHash,
                             error, errorSize)
               ? core::Status::fromErrno("manager staging failed")
               : core::Status();
}

} // namespace launcher
} // namespace nbt
