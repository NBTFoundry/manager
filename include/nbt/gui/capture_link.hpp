// SPDX-License-Identifier: MPL-2.0
/* GUI-side capture control writer (/dev/shmem/nbt-manager-capture). */
#ifndef NBT_GUI_CAPTURE_LINK_HPP
#define NBT_GUI_CAPTURE_LINK_HPP

#include "nbt/capture/control.h"

#include <stdint.h>

namespace nbt {
namespace gui {

class WorkerChannel;
class UiController;

class CaptureLink {
public:
    CaptureLink();

    bool start(const char *path, uint64_t nowMs);
    void setController(const nbt_ccs_config &ccs);
    /* The record always carries the 1.5 s watchdog and the shim's 5 s BACK
     * escape, installs included: the GUI only ignores its own escape then. */
    void sync(bool wantOn, uint64_t nowMs);
    void latchEscape();
    void setOff();

    bool escaped() const {
        return escaped_;
    }
    uint64_t generation() const {
        return ctrl_.generation;
    }

private:
    bool writeNow();

    const char *path_;
    nbt_capture_control ctrl_;
    bool initialized_;
    bool escaped_;
    bool lastOn_;
    uint64_t nextRefreshMs_;
};

/* Flushes a queued STOP <app> fault synchronously when 5s escape closes GUI. */
void flushEscapeStop(WorkerChannel &channel, UiController &ui);

} // namespace gui
} // namespace nbt
#endif
