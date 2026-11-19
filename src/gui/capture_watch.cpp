// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/capture_watch.hpp"

#include "nbt/capture/diag.h"
#include "nbt/gui/text.hpp"
#include "nbt/log.h"

#include <errno.h>
#include <signal.h>

namespace nbt {
namespace gui {
namespace {

/* The note on the manager's list, in the current language. */
const char *noteText(CaptureWatch::State state) {
    switch (state) {
    case CaptureWatch::Escaped:
        return text(T_KEYS_TOO_ESCAPE);
    case CaptureWatch::Observer:
        return text(T_KEYS_TOO_OBSERVER);
    case CaptureWatch::NotLoaded:
        return text(T_KEYS_TOO_NO_SHIM);
    default:
        return "";
    }
}

/* The state on Settings > Status, in the current language. */
const char *stateText(CaptureWatch::State state) {
    switch (state) {
    case CaptureWatch::Blocking:
        return text(T_KEYS_KEPT);
    case CaptureWatch::Escaped:
        return text(T_KEYS_ESCAPED);
    case CaptureWatch::Observer:
        return text(T_KEYS_OBSERVER);
    case CaptureWatch::NotLoaded:
        return text(T_KEYS_NO_SHIM);
    default:
        return text(T_UNKNOWN);
    }
}

/* The state in the debug log, in English. */
const char *name(CaptureWatch::State state) {
    switch (state) {
    case CaptureWatch::Blocking:
        return "kept from iDrive";
    case CaptureWatch::Escaped:
        return "escaped (BACK held 5 s): iDrive gets them";
    case CaptureWatch::Observer:
        return "observer shim: iDrive gets them";
    case CaptureWatch::NotLoaded:
        return "no shim in iDrive: iDrive gets them";
    default:
        return "unknown";
    }
}

} // namespace

CaptureWatch::CaptureWatch() : state_(Unknown), nextMs_(0) {
}

CaptureWatch::State CaptureWatch::classify(const nbt_capture_diag_shm *snap, uint64_t generation,
                                           bool alive) {
    if (!snap || !alive) {
        return NotLoaded;
    }
    if (snap->observer_mode) {
        return Observer;
    }
    for (unsigned i = 0; i < NBT_CAPTURE_DIAG_SLOTS; ++i) {
        const nbt_capture_diag_slot &slot = snap->slots[i];
        if (slot.seq && slot.seq == slot.seq_end && slot.kind == NBT_CAPTURE_DIAG_ESCAPE &&
            slot.generation == generation) {
            return Escaped;
        }
    }
    return Blocking;
}

bool CaptureWatch::poll(const char *path, uint64_t generation, bool wanted, uint64_t nowMs) {
    if (!path || !wanted || nowMs < nextMs_) {
        return false;
    }
    nextMs_ = nowMs + 1000;
    nbt_capture_diag_shm snap;
    const bool read = nbt_capture_diag_read(path, &snap) == 0;
    // The ring outlives an iDrive that restarted without the shim: its writer must exist.
    const bool alive = read && (kill(static_cast<pid_t>(snap.pid), 0) == 0 || errno == EPERM);
    const State next = classify(read ? &snap : NULL, generation, alive);
    if (next == state_) {
        return false;
    }
    nbt_logf("capture: controller keys %s (generation %llu)", name(next),
             static_cast<unsigned long long>(generation));
    state_ = next;
    return true;
}

const char *CaptureWatch::note() const {
    return noteText(state_);
}

const char *CaptureWatch::describe() const {
    return stateText(state_);
}

} // namespace gui
} // namespace nbt
