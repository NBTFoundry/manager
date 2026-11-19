// SPDX-License-Identifier: MPL-2.0
/* GUI-side capture control state & synchronous escape stop helper. */
#include "nbt/gui/capture_link.hpp"

#include "nbt/core/system.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/worker_channel.hpp"
#include "nbt/log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace gui {
namespace {

uint64_t nextGeneration(const char *path, uint64_t nowMs) {
    nbt_capture_control prev;
    if (path && path[0] && nbt_capture_control_read(path, &prev) == 0 && prev.generation > 0u &&
        prev.generation < UINT64_MAX - 1u) {
        return prev.generation + 1u;
    }
    return nowMs > 0u ? nowMs : 1u;
}

} // namespace

CaptureLink::CaptureLink()
    : path_(NULL), ctrl_(), initialized_(false), escaped_(false), lastOn_(false),
      nextRefreshMs_(0) {
}

bool CaptureLink::writeNow() {
    if (!initialized_ || !path_ || !path_[0]) {
        return true;
    }
    return nbt_capture_control_write(path_, &ctrl_) == 0;
}

bool CaptureLink::start(const char *path, uint64_t nowMs) {
    path_ = path;
    escaped_ = false;
    lastOn_ = false;
    nextRefreshMs_ = nowMs;
    const pid_t self = getpid();
    const uint64_t sid = ((nowMs << 16) ^ static_cast<uint64_t>(self)) | 1u;
    const uint64_t gen = nextGeneration(path_, nowMs);
    nbt_capture_control_init(&ctrl_, static_cast<int32_t>(self), sid, gen, NULL);
    ctrl_.state = NBT_CAPTURE_OFF;
    initialized_ = true;
    if (!writeNow()) {
        nbt_logf("capture control file %s: errno %d", path_ ? path_ : "(null)", errno);
        initialized_ = false;
        return false;
    }
    return true;
}

void CaptureLink::setController(const nbt_ccs_config &ccs) {
    if (!nbt_ccs_config_valid(&ccs)) {
        return;
    }
    ctrl_.knob_instance = ccs.knob_instance;
    ctrl_.ccs_instance = ccs.ccs_instance;
    ctrl_.instance_count = ccs.instance_count;
    memcpy(ctrl_.instances, ccs.instances, ccs.instance_count);
    ctrl_.knob_back_slot = ccs.knob_back_slot;
    ctrl_.ccs_back_slot = ccs.ccs_back_slot;
    (void)writeNow();
}

void CaptureLink::sync(bool wantOn, uint64_t nowMs) {
    if (!initialized_) {
        return;
    }
    const bool effectiveOn = !escaped_ && wantOn;
    if (effectiveOn == lastOn_ && (!effectiveOn || nowMs < nextRefreshMs_)) {
        return;
    }
    ctrl_.state = effectiveOn ? NBT_CAPTURE_ON : NBT_CAPTURE_OFF;
    ctrl_.watchdog_ms = NBT_CAPTURE_DEFAULT_WATCHDOG_MS;
    ctrl_.escape_enabled = 1u;
    if (writeNow()) {
        lastOn_ = effectiveOn;
        nextRefreshMs_ = nowMs + 500u;
    }
}

void CaptureLink::latchEscape() {
    escaped_ = true;
    ctrl_.state = NBT_CAPTURE_OFF;
    lastOn_ = false;
    (void)writeNow();
}

void CaptureLink::setOff() {
    ctrl_.state = NBT_CAPTURE_OFF;
    lastOn_ = false;
    (void)writeNow();
}

void flushEscapeStop(WorkerChannel &channel, UiController &ui) {
    if (!ui.target[0] || !ui.faultStop) {
        return;
    }
    Request stopReq;
    const int n = snprintf(stopReq.command, sizeof(stopReq.command), "STOP %s fault", ui.target);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(stopReq.command)) {
        return;
    }
    stopReq.timeoutMs = 5000u;
    const core::Result<uint64_t> startMs = core::monotonicMs();
    if (!startMs.get()) {
        return;
    }
    const uint64_t deadline = startMs.value() + 2000u;
    char replyText[kReplyMax];
    char cmd[kCommandMax];
    bool ok = false;
    bool sent = false;
    for (;;) {
        if (channel.take(replyText, sizeof(replyText), &ok, cmd, sizeof(cmd)) && sent) {
            break;
        }
        if (!sent && !channel.busy() && channel.connected()) {
            sent = channel.submit(stopReq);
            if (!sent) {
                break;
            }
        }
        const core::Result<uint64_t> now = core::monotonicMs();
        if (!now.get() || now.value() >= deadline || !channel.connected()) {
            break;
        }
        const struct timespec pause = {0, 10 * 1000000L};
        nanosleep(&pause, NULL);
    }
}

} // namespace gui
} // namespace nbt
