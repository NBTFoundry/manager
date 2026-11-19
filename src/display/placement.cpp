// SPDX-License-Identifier: MPL-2.0
#include "nbt/display/placement.hpp"

#include "nbt/display/layer_manager.hpp"
#include "nbt/log.h"

#include <errno.h>

namespace nbt {
namespace display {
namespace {

const uint64_t kBindMs = 10000u, kRetryMs = 250u, kCheckMs = 1000u;

} // namespace

Placement::Placement()
    : surface_(0), overlay_(0), state_(Off), nextMs_(0), giveUpMs_(0), unbound_(0) {
}

Placement::~Placement() {
    clear();
}

void Placement::place(int surface, uint64_t nowMs) {
    // From one of ours on the panel to the other, it stays until show() swaps
    // them, so the held (frozen) iDrive screen never shows in between.
    if (state_ != OnPanel) {
        clear();
    }
    surface_ = surface;
    state_ = Waiting;
    nextMs_ = nowMs; // LayerManager makes the surface about a second after the first post
    giveUpMs_ = nowMs + kBindMs;
}

Placement::State Placement::service(uint64_t nowMs) {
    if ((state_ != Waiting && state_ != OnPanel) || nowMs < nextMs_) {
        return state_;
    }
    if (state_ == OnPanel) {
        // Android Auto can take the layer without iDrive's focus moving, and a
        // closed window takes its surface with it: input must stop either way.
        // A crashed application's surface stays in the render order after its
        // window closes (dev71: Doom; the screen stayed black), so the window
        // is checked too: twice in a row, as one failed query must not count.
        nextMs_ = nowMs + kCheckMs;
        unbound_ = surfaceBound(surface_) ? 0 : unbound_ + 1;
        if (holds(surface_) == 0 || unbound_ >= 2) {
            state_ = Lost;
            nbt_logf("surface %d: %s", surface_,
                     unbound_ >= 2 ? "its window is gone" : "no longer on the panel");
        }
        return state_;
    }
    errno = 0; // set only when LayerManagerControl could not run
    if (!surfaceBound(surface_)) {
        nextMs_ = nowMs + kRetryMs;
        if (nowMs >= giveUpMs_) {
            state_ = Failed;
            nbt_logf("surface %d: LayerManager made none within 10 s (errno %d)", surface_, errno);
        }
        return state_;
    }
    int replaced = 0;
    const core::Status status = show(surface_, &replaced, overlay_);
    if (!status.ok()) {
        state_ = Failed;
        nbt_logf("surface %d: %s", surface_, status.message());
        (void)restore();
        return state_;
    }
    state_ = OnPanel;
    unbound_ = 0;
    nextMs_ = nowMs + kCheckMs;
    if (replaced == 256) {
        nbt_logf("surface %d: on the panel in place of iDrive", surface_);
    } else {
        nbt_logf("surface %d: on the panel in place of surface %d", surface_, replaced);
    }
    return state_;
}

void Placement::clear() {
    if (state_ != Off) {
        nbt_logf("surface %d: %s", surface_,
                 restore() ? "iDrive restored" : "iDrive already had the panel");
    }
    surface_ = 0;
    unbound_ = 0;
    state_ = Off;
    nextMs_ = giveUpMs_ = 0;
}

} // namespace display
} // namespace nbt
