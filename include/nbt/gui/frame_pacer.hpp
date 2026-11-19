#ifndef NBT_GUI_FRAME_PACER_HPP
#define NBT_GUI_FRAME_PACER_HPP

/* 30 fps for the manager's window: the GUI's loop comes round at least once a
 * frame (at once for input or a worker reply) and draws a changed view at most
 * once per frame. Frames that take longer than one are counted and reported in
 * the debug log at most every 10 s, so a car run shows whether 30 fps holds. */

#include <stdint.h>

namespace nbt {
namespace gui {

static const uint64_t kFrameMs = 33u;

class FramePacer {
public:
    FramePacer() : nextDrawMs_(0), reportMs_(0), slowFrames_(0), slowestMs_(0) {
    }
    /* A changed view may be drawn now. */
    bool due(uint64_t nowMs) const {
        return nowMs >= nextDrawMs_;
    }
    /* How long the loop may wait: one frame, or until a changed view's slot. */
    uint64_t waitMs(uint64_t nowMs, bool changed) const;
    /* A frame was drawn from `startMs` to `endMs`. */
    void drawn(uint64_t startMs, uint64_t endMs);

private:
    uint64_t nextDrawMs_;
    uint64_t reportMs_;
    unsigned slowFrames_;
    unsigned slowestMs_;
};

} // namespace gui
} // namespace nbt

#endif
