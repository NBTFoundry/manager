#include "nbt/gui/capture_watch.hpp"

#include "nbt/capture/diag.h"
#include "nbt/log.h"

#include <errno.h>
#include <signal.h>

namespace nbt {
namespace gui {
namespace {

const char *text(CaptureWatch::State state) {
    switch (state) {
    case CaptureWatch::Escaped:
        return "iDrive gets the keys too (BACK held 5 s)";
    case CaptureWatch::Observer:
        return "iDrive gets the keys too (shim not blocking)";
    case CaptureWatch::NotLoaded:
        return "iDrive gets the keys too (shim not loaded)";
    default:
        return "";
    }
}

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
    return text(state_);
}

const char *CaptureWatch::describe() const {
    return name(state_);
}

} // namespace gui
} // namespace nbt
