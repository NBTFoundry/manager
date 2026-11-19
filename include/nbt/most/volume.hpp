#ifndef NBT_MOST_VOLUME_HPP
#define NBT_MOST_VOLUME_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace most {

/* Car master volume indicator: MOST AudioMaster (FBlock 0x20) fkt 0x0C08
 * (VisibleVolumeIndicator), op Status (0x0C), 5-byte payload [.. .. level .. max].
 * Repeats within kDupMs are suppressed; returns percent 0..100. */
class VolumeLevel {
public:
    enum { kFBlock = 0x20, kFkt = 0xC08, kOpStatus = 0x0C, kDupMs = 40 };

    VolumeLevel() : hasLast_(false), lastLevel_(0), lastMax_(0), lastMs_(0) {
    }

    bool feed(unsigned fblock, unsigned fkt, unsigned op, const uint8_t *data, size_t length,
              uint64_t nowMs, int &percent) {
        if (fblock != kFBlock || fkt != kFkt || op != kOpStatus || !data || length < 5) {
            return false;
        }
        const unsigned level = data[2];
        const unsigned max = data[4];
        if (max == 0 || level > max) {
            return false;
        }
        if (hasLast_ && level == lastLevel_ && max == lastMax_ && nowMs >= lastMs_ &&
            nowMs - lastMs_ < static_cast<uint64_t>(kDupMs)) {
            return false;
        }
        hasLast_ = true;
        lastLevel_ = level;
        lastMax_ = max;
        lastMs_ = nowMs;
        const unsigned pct = level * 100u / max;
        percent = static_cast<int>(pct > 100u ? 100u : pct);
        return true;
    }

    void reset() {
        hasLast_ = false;
    }

private:
    bool hasLast_;
    unsigned lastLevel_;
    unsigned lastMax_;
    uint64_t lastMs_;
};

} // namespace most
} // namespace nbt

#endif
