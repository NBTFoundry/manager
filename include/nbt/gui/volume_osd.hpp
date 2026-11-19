// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_VOLUME_OSD_HPP
#define NBT_GUI_VOLUME_OSD_HPP

#include "nbt/display/placement.hpp"
#include "nbt/gui/screen_window.hpp"

#include <stdint.h>

namespace nbt {
namespace gui {

/* Transparent full-panel QNX Screen volume bar overlay on LayerManager surface
 * 1292 (above both the manager GUI 1290 and any foreground app 1291), driven
 * by MOST AudioMaster 0x20 / 0x0C08 notifications. */
class VolumeOsd {
public:
    VolumeOsd();
    ~VolumeOsd();

    bool start(int panelWidth, int panelHeight);
    void attachInput();
    void stop();

    void onVolume(int pct, uint64_t nowMs);
    void service(uint64_t nowMs, display::Placement &panel);

    int volumeFd() const {
        return volumeFd_;
    }
    uint64_t nextWakeMs(uint64_t nowMs) const;

private:
    VolumeOsd(const VolumeOsd &);
    VolumeOsd &operator=(const VolumeOsd &);

    void clearSurface();
    void renderPill(int pct);

    ScreenWindow window_;
    int volumeFd_;
    bool started_;
    bool overlayReady_;
    bool overlayFailed_;
    bool shown_;
    bool pending_;
    int pct_;
    int drawnPct_;
    uint32_t lastSeq_;
    unsigned probeCount_;
    uint64_t nextProbeMs_;
    uint64_t hideAtMs_;
};

} // namespace gui
} // namespace nbt

#endif
