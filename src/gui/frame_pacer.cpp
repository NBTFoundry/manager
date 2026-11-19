#include "nbt/gui/frame_pacer.hpp"

#include "nbt/log.h"

namespace nbt {
namespace gui {
namespace {

const uint64_t kReportMs = 10000u;

} // namespace

uint64_t FramePacer::waitMs(uint64_t nowMs, bool changed) const {
    if (changed && nextDrawMs_ > nowMs && nextDrawMs_ - nowMs < kFrameMs) {
        return nextDrawMs_ - nowMs;
    }
    return kFrameMs;
}

void FramePacer::drawn(uint64_t startMs, uint64_t endMs) {
    nextDrawMs_ = startMs + kFrameMs;
    const uint64_t took = endMs >= startMs ? endMs - startMs : 0;
    if (took > kFrameMs) {
        ++slowFrames_;
        slowestMs_ = took > slowestMs_ ? static_cast<unsigned>(took) : slowestMs_;
    }
    if (slowFrames_ && endMs >= reportMs_ + kReportMs) {
        nbt_logf("%u frame(s) slower than %u ms in %u s, slowest %u ms", slowFrames_,
                 static_cast<unsigned>(kFrameMs), static_cast<unsigned>(kReportMs / 1000),
                 slowestMs_);
        reportMs_ = endMs;
        slowFrames_ = slowestMs_ = 0;
    }
}

} // namespace gui
} // namespace nbt
