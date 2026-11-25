// SPDX-License-Identifier: MPL-2.0
#include "app_host_supervisor.hpp"

#include "nbt/app_faults.h"
#include "nbt/app_host.h"
#include "nbt/autostart.h"
#include "nbt/log.h"
#include "nbt/process/host_control.hpp"
#include "nbt/process/host_signals.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

namespace nbt {
namespace process {

bool AppHostSupervisor::advertiseCapability(bool enabled, off_t byte, bool &advertised) {
    if (!enabled || !child_.ready || advertised) {
        return true;
    }
    if (HostControl::setByteLock(hostLock_, F_RDLCK, byte)) {
        return false;
    }
    advertised = true;
    return true;
}

// A service's run ended: what the next one prepares again is released.
void AppHostSupervisor::endRun(int result) {
    if (result && child_.pid > 0) {
        output_.stopChild(child_); // a broken contract: it may still run
    }
    output_.drain();
    bridge_.stop();
    channels_.stop();
    child_ = nbt_process();
    child_.ready_fd = child_.lease_fd = -1;
}

// Waits `ms`, copying the output; false when stopped meanwhile.
bool AppHostSupervisor::pauseFor(uint64_t ms) {
    uint64_t start = 0, now = 0;
    if (nbt_monotonic_ms(&start)) {
        return false;
    }
    while (!hostStopping() && !nbt_monotonic_ms(&now) && now - start < ms) {
        struct pollfd out = {output_.fd(), POLLIN, 0};
        if (poll(&out, 1, kHostPollMs) > 0 && out.revents) {
            output_.drain();
        }
    }
    return !hostStopping() && now - start >= ms;
}

int AppHostSupervisor::superviseLoop() {
    uint64_t now = 0;
    while (!hostStopping()) {
        struct pollfd ready[2] = {{channels_.wakeFd(), POLLIN, 0}, {output_.fd(), POLLIN, 0}};
        if ((poll(ready, 2, kHostPollMs) < 0 && errno != EINTR) || nbt_monotonic_ms(&now)) {
            return 1;
        }
        if (ready[1].revents) {
            output_.drain();
        }
        output_.log().syncDue();
        channels_.poll(now, ready[0].revents);
        bridge_.poll(now, child_.ready != 0);
        pause_.hold(child_, now);
        const int status = nbt_process_poll(&child_, now);
        if (status != 1) {
            if (status) {
                nbt_logf("host %s: contract broken; stopping", app_);
            } else {
                // Also here: the application log may be what fails (dev74).
                nbt_logf("host %s: application exited (status %d, signal %d, fault %d)", app_,
                         child_.exit_status, child_.exit_signal, child_.fault);
            }
            if (status == 0 || child_.fault) {
                if (output_.ended(child_)) { // a crash; a signal's report gets the system log
                    (void)output_.report(fs_, app_,
                                         child_.exit_signal || child_.exit_status >= 128);
                }
            } else {
                output_.log().note("stopped: its host lost track of it");
            }
            return status ? 1 : 0;
        }
        if (hostStopping()) {
            break;
        }
        if (nbt_process_healthy(&child_, now)) {
            if (!faultCleared_ && !nbt_app_fault_clear(fs_, app_)) {
                faultCleared_ = true;
            }
            if (!confirmed_ && !nbt_autostart_clear(fs_, app_)) {
                confirmed_ = true;
                nbt_logf("host %s: autostart healthy; boot guard cleared", app_);
            }
        }
        if (!advertiseCapability(forwardsShow_, 1, advertised_) ||
            !advertiseCapability(canStatus_, 3, statusAdvertised_)) {
            return 1;
        }
        const bool pauseNow = takePause() && !service_;
        const int step =
            pause_.service(app_, child_, channels_, output_.log(), now, pauseNow, showPending());
        if (step) {
            if (step > 0) {
                stopHost(); // paused for 2 minutes
            }
            return step > 0 ? 0 : 1;
        }
        if (showPending()) {
            showDone();
            if (advertised_) {
                if (kill(child_.pid, SIGUSR1) && errno != ESRCH) {
                    return 1;
                }
                nbt_logf("host %s: show forwarded", app_);
            }
        }
    }
    return 0;
}

void AppHostSupervisor::shutdown(int result) {
    exitCode_ = result;
    if ((advertised_ && HostControl::setByteLock(hostLock_, F_UNLCK, 1)) ||
        (statusAdvertised_ && HostControl::setByteLock(hostLock_, F_UNLCK, 3))) {
        exitCode_ = 1;
    }
    closeHostFd(statusFd_);
    if (child_.pid > 0) {
        if (hostStopping()) {
            nbt_logf("host %s: stopping", app_);
        }
        pause_.beforeStop(child_);
        output_.stopChild(child_);
        output_.drain();
        if (hostStopping()) {
            output_.log().note("stopped by the manager");
        }
    }
    // A service's faults never block its application (docs/service-bridge.md).
    if (!service_ &&
        (result != 0 || (!hostStopping() && !faultCleared_ &&
                         (child_.fault || child_.exit_signal || child_.exit_status)))) {
        (void)nbt_app_fault_record(fs_, app_);
    }
    bridge_.stop();
    channels_.stop();
    nbt_logf("host %s: ended", app_);
    closeHostFd(surfaceFd_);
    closeHostFd(hostLock_);
}

} // namespace process
} // namespace nbt
