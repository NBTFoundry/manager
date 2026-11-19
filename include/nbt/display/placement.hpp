// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DISPLAY_PLACEMENT_HPP
#define NBT_DISPLAY_PLACEMENT_HPP

#include <stdint.h>

namespace nbt {
namespace display {

/* One of the manager's surfaces on the panel in place of iDrive: put there
 * once LayerManager has made it (within 10 s), then checked once a second.
 * The caller places only while the plugin holds iDrive on its inert state.
 *
 * Failed: never made, or show() refused (Android Auto has the panel).
 * Lost: it was on the panel and no longer is (another surface took the
 * layer, or its window closed). Both are final until the next place(). */
class Placement {
public:
    enum State { Off, Waiting, OnPanel, Failed, Lost };

    Placement();
    ~Placement();

    // clear()s first, unless one of ours is on the panel: show() swaps them.
    void place(int surface, uint64_t nowMs);
    State service(uint64_t nowMs);
    void clear(); // iDrive back, unless another owner has the panel
    void setOverlay(int surface) {
        overlay_ = surface;
    }

    State state() const {
        return state_;
    }
    int surface() const {
        return surface_;
    }
    bool active(int surface) const { // placed, on the panel or on its way
        return surface_ == surface && (state_ == Waiting || state_ == OnPanel);
    }
    bool onPanel(int surface) const {
        return surface_ == surface && state_ == OnPanel;
    }

private:
    Placement(const Placement &);
    Placement &operator=(const Placement &);

    int surface_;
    int overlay_;
    State state_;
    uint64_t nextMs_;
    uint64_t giveUpMs_;
    unsigned unbound_; // checks in a row that found the surface without a window
};

} // namespace display
} // namespace nbt

#endif
