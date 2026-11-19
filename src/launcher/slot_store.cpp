// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/slot_store.hpp"

#include "nbt/launcher/slot_fs.hpp"
#include "nbt/launcher/slot_state.hpp"
#include "nbt/transaction.h"

#include <errno.h>
#include <unistd.h>

namespace {

using nbt::launcher::SlotFs;
using nbt::launcher::SlotState;

int closeLockWithErrno(int lock, int result) {
    const int saved = errno;
    close(lock);
    errno = saved;
    return result;
}

int loadStateLocked(nbt_fs *fs, nbt_manager_state *state) {
    const int current = nbt_fs_exists(fs, NBT_FS_MANAGER_STATE, "manager", NULL);
    const int next = nbt_fs_exists(fs, NBT_FS_MANAGER_STATE_NEXT, "manager", NULL);
    if (current < 0 || next < 0) {
        return -1;
    }
    if (!current) {
        errno = next ? EBUSY : ENOENT;
        return -1;
    }
    if (SlotFs::readState(fs, state)) {
        return -1;
    }
    if (next && (nbt_fs_unlink(fs, NBT_FS_MANAGER_STATE_NEXT, "manager", NULL) ||
                 SlotFs::managerBarrier(fs))) {
        return -1;
    }
    return 0;
}

int transition(nbt_fs *fs, const nbt_release *release, int mode, const nbt_process *process,
               uint64_t nowMs) {
    nbt_manager_state state;
    if (!fs) {
        errno = EINVAL;
        return -1;
    }
    const int lock = nbt_tx_lock(fs, "manager");
    if (lock < 0) {
        return -1;
    }
    int result = -1;
    if (!loadStateLocked(fs, &state)) {
        if ((mode == 0 && nbt_manager_state_begin(&state, release, 0)) ||
            (mode == 1 && nbt_manager_state_begin(&state, release, 1)) ||
            (mode == 2 && nbt_manager_state_rollback(&state)) ||
            (mode == 3 && nbt_manager_state_confirm(&state, process, nowMs)) ||
            (mode == 4 && nbt_manager_state_trial(&state))) {
            errno = EINVAL;
        } else {
            result = SlotFs::writeState(fs, &state);
        }
    }
    return closeLockWithErrno(lock, result);
}

} // namespace

int nbt_manager_store_inspect(nbt_fs *fs, nbt_manager_state *state) {
    if (!fs || !state) {
        errno = EINVAL;
        return -1;
    }
    return SlotFs::readState(fs, state);
}

int nbt_manager_store_load(nbt_fs *fs, nbt_manager_state *state) {
    if (!fs || !state) {
        errno = EINVAL;
        return -1;
    }
    const int lock = nbt_tx_lock(fs, "manager");
    if (lock < 0) {
        return -1;
    }
    return closeLockWithErrno(lock, loadStateLocked(fs, state));
}

int nbt_manager_store_initial(nbt_fs *fs, const nbt_release *release) {
    nbt_manager_state state;
    if (!fs || nbt_manager_state_initial(&state, release)) {
        errno = EINVAL;
        return -1;
    }
    const int lock = nbt_tx_lock(fs, "manager");
    if (lock < 0) {
        return -1;
    }
    int result = -1;
    const int current = nbt_fs_exists(fs, NBT_FS_MANAGER_STATE, "manager", NULL);
    const int next = nbt_fs_exists(fs, NBT_FS_MANAGER_STATE_NEXT, "manager", NULL);
    if (current == 0 && next == 0) {
        result = SlotFs::writeState(fs, &state);
    } else if (current >= 0 && next >= 0) {
        errno = current ? EEXIST : EBUSY;
    }
    return closeLockWithErrno(lock, result);
}

int nbt_manager_store_resume_initial(nbt_fs *fs, const nbt_release *release) {
    nbt_manager_state state;
    if (!fs || nbt_manager_state_initial(&state, release)) {
        errno = EINVAL;
        return -1;
    }
    const int lock = nbt_tx_lock(fs, "manager");
    if (lock < 0) {
        return -1;
    }
    int result = -1;
    const int current = nbt_fs_exists(fs, NBT_FS_MANAGER_STATE, "manager", NULL);
    const int next = nbt_fs_exists(fs, NBT_FS_MANAGER_STATE_NEXT, "manager", NULL);
    if (current != 0 || next < 0) {
        if (current > 0) {
            errno = EEXIST;
        }
    } else if (!next || (!nbt_fs_unlink(fs, NBT_FS_MANAGER_STATE_NEXT, "manager", NULL) &&
                         !SlotFs::managerBarrier(fs))) {
        result = SlotFs::writeState(fs, &state);
    }
    return closeLockWithErrno(lock, result);
}

int nbt_manager_store_begin(nbt_fs *fs, const nbt_release *release, int recovery) {
    if (recovery != 0 && recovery != 1) {
        errno = EINVAL;
        return -1;
    }
    return transition(fs, release, recovery, NULL, 0);
}

int nbt_manager_store_rollback(nbt_fs *fs) {
    return transition(fs, NULL, 2, NULL, 0);
}

int nbt_manager_store_trial(nbt_fs *fs) {
    return transition(fs, NULL, 4, NULL, 0);
}

int nbt_manager_store_confirm(nbt_fs *fs, const nbt_process *process, uint64_t now_ms) {
    return transition(fs, NULL, 3, process, now_ms);
}

int nbt_manager_store_restart(nbt_fs *fs, uint64_t epoch_seconds, unsigned *delay_ms) {
    nbt_manager_state state;
    if (!fs || !delay_ms) {
        errno = EINVAL;
        return -1;
    }
    const int lock = nbt_tx_lock(fs, "manager");
    if (lock < 0) {
        return -1;
    }
    int result = -1;
    if (!loadStateLocked(fs, &state)) {
        nbt::core::Result<SlotState> value = SlotState::fromLegacy(state);
        if (!value.ok()) {
            errno = EINVAL;
        } else if (value.value().recordRestart(epoch_seconds, delay_ms) ==
                   nbt::launcher::RestartDecision::Exhausted) {
            result = 1;
            errno = ELOOP;
        } else {
            state = value.value().legacy();
            result = SlotFs::writeState(fs, &state);
        }
    }
    return closeLockWithErrno(lock, result);
}

int nbt_manager_store_clear_restarts(nbt_fs *fs) {
    nbt_manager_state state;
    if (!fs) {
        errno = EINVAL;
        return -1;
    }
    const int lock = nbt_tx_lock(fs, "manager");
    if (lock < 0) {
        return -1;
    }
    int result = -1;
    if (!loadStateLocked(fs, &state)) {
        if (!state.restarts) {
            result = 0;
        } else {
            nbt::core::Result<SlotState> value = SlotState::fromLegacy(state);
            if (!value.ok()) {
                errno = EINVAL;
            } else {
                value.value().clearRestarts();
                state = value.value().legacy();
                result = SlotFs::writeState(fs, &state);
            }
        }
    }
    return closeLockWithErrno(lock, result);
}

namespace nbt {
namespace launcher {
namespace {

core::Status call(int res, const char *message) {
    return res ? core::Status::fromErrno(message) : core::Status();
}

} // namespace

core::Status SlotStore::load(nbt_manager_state &state) const {
    return call(nbt_manager_store_load(const_cast<nbt_fs *>(root_.legacy()), &state),
                "manager state load failed");
}

core::Status SlotStore::initial(const nbt_release &release) const {
    return call(nbt_manager_store_initial(const_cast<nbt_fs *>(root_.legacy()), &release),
                "manager state initialization failed");
}

core::Status SlotStore::resumeInitial(const nbt_release &release) const {
    return call(nbt_manager_store_resume_initial(const_cast<nbt_fs *>(root_.legacy()), &release),
                "manager initial state recovery failed");
}

core::Status SlotStore::begin(const nbt_release &release, bool recovery) const {
    return call(nbt_manager_store_begin(const_cast<nbt_fs *>(root_.legacy()), &release, recovery),
                "manager state begin failed");
}

core::Status SlotStore::trial() const {
    return call(nbt_manager_store_trial(const_cast<nbt_fs *>(root_.legacy())),
                "manager trial transition failed");
}

core::Status SlotStore::rollback() const {
    return call(nbt_manager_store_rollback(const_cast<nbt_fs *>(root_.legacy())),
                "manager rollback failed");
}

core::Status SlotStore::confirm(const nbt_process &process, uint64_t nowMs) const {
    return call(nbt_manager_store_confirm(const_cast<nbt_fs *>(root_.legacy()), &process, nowMs),
                "manager confirmation failed");
}

core::Result<unsigned> SlotStore::recordRestart(uint64_t epochSeconds) const {
    unsigned delay = 0;
    const int result =
        nbt_manager_store_restart(const_cast<nbt_fs *>(root_.legacy()), epochSeconds, &delay);
    if (result) {
        return core::Result<unsigned>::failure(core::Status::error(
            result > 0 ? ELOOP : errno,
            result > 0 ? "manager restart limit reached" : "manager restart record failed"));
    }
    return core::Result<unsigned>::success(delay);
}

core::Status SlotStore::clearRestarts() const {
    return call(nbt_manager_store_clear_restarts(const_cast<nbt_fs *>(root_.legacy())),
                "manager restart reset failed");
}

} // namespace launcher
} // namespace nbt
