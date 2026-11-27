// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/slot_select.hpp"

#include "nbt/log.h"
#include "nbt/manager_stage.h"

#include <errno.h>

namespace nbt {
namespace launcher {

core::Status selectSlot(storage::ManagedRoot &root, SlotStore &store, nbt_manager_state &state,
                        bool &trial) {
    core::Status status = store.load(state);
    if (!status.ok()) {
        return status;
    }
    trial = false;
    if (state.pending) {
        if (nbt_manager_verify_slot(root.legacy(), &state.previous)) {
            return core::Status::error(errno ? errno : EIO,
                                       "known-good manager slot verification failed");
        }
        if (state.pending == 2 || nbt_manager_verify_slot(root.legacy(), &state.active)) {
            status = store.rollback();
            if (!status.ok()) {
                return status;
            }
            status = store.load(state);
            if (!status.ok()) {
                return status;
            }
        } else {
            status = store.trial();
            if (!status.ok()) {
                return status;
            }
            trial = true;
        }
    }
    if (!trial && nbt_manager_verify_slot(root.legacy(), &state.active)) {
        return core::Status::error(errno ? errno : EIO, "active manager slot verification failed");
    }
    nbt_logf("slot %u verified, release %llu%s", state.active.slot,
             static_cast<unsigned long long>(state.active.sequence), trial ? " (trial)" : "");
    // Each selection gets fresh attempts; a trial keeps counting toward rollback.
    if (!trial && state.restarts) {
        status = store.clearRestarts();
        if (!status.ok()) {
            return status;
        }
        state.restarts = 0;
    }
    return core::Status();
}

} // namespace launcher
} // namespace nbt
