#include "nbt/gui/plugin_link.hpp"

#include <unistd.h>

namespace nbt {
namespace gui {

bool PluginLink::start(const char *signalPath, const char *livenessPath, uint64_t nowMs) {
    HoldSignal signal = SignalShow;
    signalPath_ = signalPath;
    livenessPath_ = livenessPath;
    const HoldReadResult result = holdSignalRead(signalPath_, sequence_, signal);
    have_ = result != HoldReadError; // absent: every later line is new
    if (result != HoldReadOk) {
        sequence_ = 0;
    }
    beatMs_ = nowMs;
    return holdLivenessWrite(livenessPath_, getpid(), nowMs) == 0;
}

bool PluginLink::beat(uint64_t nowMs) {
    if (nowMs < beatMs_ + 500u) {
        return true;
    }
    beatMs_ = nowMs;
    return holdLivenessWrite(livenessPath_, getpid(), nowMs) == 0;
}

bool PluginLink::next(HoldSignal &signal) {
    uint64_t sequence = 0;
    HoldSignal read = SignalShow;
    if (holdSignalRead(signalPath_, sequence, read) != HoldReadOk) {
        return false;
    }
    if (!have_) { // the start's read failed: this line is the baseline
        sequence_ = sequence;
        have_ = true;
        return false;
    }
    if (sequence <= sequence_) {
        return false;
    }
    sequence_ = sequence;
    signal = read;
    return true;
}

} // namespace gui
} // namespace nbt
