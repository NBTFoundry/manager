/* The GUI session's heartbeats and its wait between loop turns. */
#include "nbt/gui/gui_session.hpp"

#include "nbt/core/system.hpp"

#include <errno.h>
#include <sys/select.h>
#include <unistd.h>

namespace nbt {
namespace gui {

/* 'R' each second while this loop runs and the worker answers (the channel's
 * thread knows; a request in flight within its deadline counts). A silent
 * worker stops the beat, and the launcher restarts both. */
int GuiSession::sendReadyHeartbeat() {
    if (config_.readyFd < 0 || nowMs_ < heartbeatMs_ || !channel_.alive(nowMs_)) {
        return 0;
    }
    if (write(config_.readyFd, "R", 1) != 1) {
        return -1;
    }
    heartbeatMs_ = nowMs_ + 1000u;
    return 0;
}

void GuiSession::beatWhileWaiting(void *session) {
    GuiSession *self = static_cast<GuiSession *>(session);
    const core::Result<uint64_t> now = core::monotonicMs();
    if (now.get() && now.value() > self->nowMs_) {
        self->nowMs_ = now.value(); // a failed beat is seen again by the loop
        (void)self->plugin_.beat(self->nowMs_);
        (void)self->sendReadyHeartbeat();
    }
}

int GuiSession::waitInput() {
    fd_set readers;
    uint64_t waitMs = pacer_.waitMs(nowMs_, !hidden_ && view_ != shown_);
    const uint64_t osdWait = volumeOsd_.nextWakeMs(nowMs_);
    if (osdWait > 0 && osdWait < waitMs) {
        waitMs = osdWait;
    }
    if (backHoldMs_ > 0 && !ui_.installing()) {
        const uint64_t target = backHoldMs_ + NBT_CAPTURE_ESCAPE_HOLD_MS;
        const uint64_t rem = nowMs_ < target ? target - nowMs_ : 1u;
        if (rem < waitMs) {
            waitMs = rem;
        }
    }
    struct timeval wait = {0, static_cast<suseconds_t>(waitMs * 1000u)};
    FD_ZERO(&readers);
    int highest = -1;
    if (inputFd_ >= 0) {
        FD_SET(inputFd_, &readers);
        highest = inputFd_;
    }
    const int gamepadFd = gamepad_.fd();
    if (gamepadFd >= 0) {
        FD_SET(gamepadFd, &readers);
        highest = gamepadFd > highest ? gamepadFd : highest;
    }
    const int volFd = volumeOsd_.volumeFd();
    if (volFd >= 0) {
        FD_SET(volFd, &readers);
        highest = volFd > highest ? volFd : highest;
    }
    const int wake = channel_.wakeFd(); // a worker reply wakes the loop at once
    if (wake >= 0) {
        FD_SET(wake, &readers);
        highest = wake > highest ? wake : highest;
    }
    const int rc = select(highest + 1, &readers, NULL, NULL, &wait);
    return (rc < 0 && errno != EINTR) ? -1 : 0;
}

} // namespace gui
} // namespace nbt
