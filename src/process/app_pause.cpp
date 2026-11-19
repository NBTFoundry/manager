#include "nbt/process/app_pause.hpp"

#include "nbt/log.h"
#include "nbt/process/host_channels.hpp"

#include <errno.h>
#include <signal.h>

namespace nbt {
namespace process {

AppPause::AppPause(uint64_t closeMs) : closeMs_(closeMs), pausedMs_(0), paused_(false) {
}

bool AppPause::pause(const nbt_process &child, HostChannels *channels, uint64_t nowMs) {
    if (paused_) {
        return true;
    }
    if (nbt_process_signal(&child, SIGSTOP) && errno != ESRCH) {
        return false;
    }
    if (channels) {
        channels->pause(true);
    }
    paused_ = true;
    pausedMs_ = nowMs;
    return true;
}

bool AppPause::resume(nbt_process &child, HostChannels *channels, uint64_t nowMs) {
    if (!paused_) {
        return true;
    }
    if (channels) {
        channels->pause(false);
    }
    paused_ = false;
    child.last_heartbeat_ms = nowMs; // its next heartbeat is due within 3 s from now
    return !nbt_process_signal(&child, SIGCONT) || errno == ESRCH;
}

void AppPause::hold(nbt_process &child, uint64_t nowMs) const {
    if (paused_ && child.ready) {
        child.last_heartbeat_ms = nowMs;
    }
}

bool AppPause::due(uint64_t nowMs) const {
    return paused_ && nowMs >= pausedMs_ && nowMs - pausedMs_ >= closeMs_;
}

int AppPause::service(const char *app, nbt_process &child, HostChannels &channels, log::AppLog &log,
                      uint64_t nowMs, bool pauseRequested, bool showRequested) {
    if (pauseRequested && child.ready && !paused_) {
        if (!pause(child, &channels, nowMs)) {
            return -1;
        }
        nbt_logf("host %s: paused while iDrive is shown", app);
        log.note("paused while iDrive is shown");
    }
    if (showRequested && paused_) {
        if (!resume(child, &channels, nowMs)) {
            return -1;
        }
        nbt_logf("host %s: resumed", app);
        log.note("resumed");
    }
    if (!due(nowMs)) {
        return 0;
    }
    nbt_logf("host %s: paused for 2 minutes; stopping", app);
    log.note("paused for 2 minutes; stopping");
    return 1;
}

void AppPause::beforeStop(const nbt_process &child) {
    if (!paused_) {
        return;
    }
    // SIGTERM stays pending while the group is stopped; SIGCONT then delivers it.
    (void)nbt_process_signal(&child, SIGTERM);
    (void)nbt_process_signal(&child, SIGCONT);
    paused_ = false;
}

} // namespace process
} // namespace nbt
