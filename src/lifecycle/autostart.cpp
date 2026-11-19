// SPDX-License-Identifier: MPL-2.0
#include "nbt/lifecycle/autostart.hpp"

#include "nbt/autostart.h"
#include "nbt/core/unique_fd.hpp"
#include "nbt/lifecycle/digit_record.hpp"
#include "nbt/lifecycle/transactions.hpp"
#include "nbt/transaction.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace lifecycle {
namespace {

int writeGuard(nbt_fs *fs, const char *app, unsigned failures) {
    return writeDigitRecord(fs, NBT_FS_AUTOSTART_GUARD_NEXT, NBT_FS_AUTOSTART_GUARD, app,
                            "NBTAUTOGUARD1\nfailures=", failures);
}

} // namespace

core::Result<nbt_autostart> Autostart::load(const storage::ManagedRoot &root, const char *app) {
    nbt_autostart state;
    memset(&state, 0, sizeof(state));
    if (!root.valid() || nbt_autostart_load(const_cast<nbt_fs *>(root.legacy()), app, &state)) {
        return core::Result<nbt_autostart>::failure(
            core::Status::fromErrno("autostart load failed"));
    }
    return core::Result<nbt_autostart>::success(state);
}

core::Status Autostart::set(const storage::ManagedRoot &root, const char *app, bool enabled) {
    if (!root.valid()) {
        return core::Status::error(core::kInvalidArgument, "managed root is not initialized");
    }
    if (nbt_autostart_set(const_cast<nbt_fs *>(root.legacy()), app, enabled ? 1 : 0)) {
        return core::Status::fromErrno("autostart save failed");
    }
    return core::Status();
}

core::Status Autostart::clear(const storage::ManagedRoot &root, const char *app) {
    if (!root.valid()) {
        return core::Status::error(core::kInvalidArgument, "managed root is not initialized");
    }
    if (nbt_autostart_clear(const_cast<nbt_fs *>(root.legacy()), app)) {
        return core::Status::fromErrno("autostart guard reset failed");
    }
    return core::Status();
}

core::Result<bool> Autostart::begin(const storage::AppTxLock &lock,
                                    const storage::ManagedRoot &root, const char *app) {
    (void)&lock;
    if (!root.valid()) {
        return core::Result<bool>::failure(
            core::Status::error(core::kInvalidArgument, "managed root is not initialized"));
    }
    const int result = nbt_autostart_begin(const_cast<nbt_fs *>(root.legacy()), app);
    if (result < 0) {
        return core::Result<bool>::failure(core::Status::fromErrno("autostart begin failed"));
    }
    return core::Result<bool>::success(result == 0);
}

} // namespace lifecycle
} // namespace nbt

int nbt_autostart_load(nbt_fs *fs, const char *app, nbt_autostart *out) {
    unsigned enabled = 0;
    if (!out ||
        !nbt::lifecycle::readDigitRecord(fs, app, NBT_FS_AUTOSTART, "NBTAUTO1\nenabled=", 1,
                                         &enabled) ||
        !nbt::lifecycle::readDigitRecord(fs, app, NBT_FS_AUTOSTART_GUARD,
                                         "NBTAUTOGUARD1\nfailures=", NBT_AUTOSTART_FAILURE_LIMIT,
                                         &out->failures)) {
        return -1;
    }
    out->enabled = static_cast<int>(enabled);
    return 0;
}

int nbt_autostart_set(nbt_fs *fs, const char *app, int enabled) {
    char text[] = "NBTAUTO1\nenabled=0\n";
    if (enabled != 0 && enabled != 1) {
        errno = EINVAL;
        return -1;
    }
    nbt::core::UniqueFd lock(nbt_tx_lock(fs, app));
    if (!lock.valid()) {
        return -1;
    }
    text[sizeof(text) - 3] = enabled ? '1' : '0';
    int result = nbt_fs_replace_text(fs, NBT_FS_AUTOSTART_NEXT, NBT_FS_AUTOSTART, app, text,
                                     sizeof(text) - 1);
    if (!lock.close().ok()) {
        result = -1;
    }
    return result;
}

int nbt_autostart_clear(nbt_fs *fs, const char *app) {
    unsigned failures = 0;
    nbt::core::UniqueFd lock(nbt_tx_lock(fs, app));
    if (!lock.valid()) {
        return -1;
    }
    int result = nbt::lifecycle::readDigitRecord(
                     fs, app, NBT_FS_AUTOSTART_GUARD,
                     "NBTAUTOGUARD1\nfailures=", NBT_AUTOSTART_FAILURE_LIMIT, &failures)
                     ? 0
                     : -1;
    if (result || failures) {
        result = nbt::lifecycle::writeGuard(fs, app, 0);
    }
    if (!lock.close().ok()) {
        result = -1;
    }
    return result;
}

int nbt_autostart_begin(nbt_fs *fs, const char *app) {
    nbt_autostart state;
    if (nbt_autostart_load(fs, app, &state)) {
        return -1;
    }
    if (!state.enabled || state.failures >= NBT_AUTOSTART_FAILURE_LIMIT) {
        return 1;
    }
    return nbt::lifecycle::writeGuard(fs, app, state.failures + 1);
}
