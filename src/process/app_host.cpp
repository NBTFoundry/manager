// SPDX-License-Identifier: MPL-2.0
#include "nbt/app_host.h"

#include "nbt/app_faults.h"
#include "nbt/app_settings.h"
#include "nbt/autostart.h"
#include "nbt/log.h"
#include "nbt/most_service.h"
#include "nbt/process.h"
#include "nbt/process/app_output.hpp"
#include "nbt/process/app_pause.hpp"
#include "nbt/process/host_bridge.hpp"
#include "nbt/process/host_control.hpp"
#include "nbt/process/host_start.hpp"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

enum { kPollMs = 200 };

volatile sig_atomic_t hostStopping;
volatile sig_atomic_t showRequested;
volatile sig_atomic_t pauseRequested; // SIGUSR2 from the GUI: iDrive took the screen

void onStop(int number) {
    (void)number;
    hostStopping = 1;
}

void onShow(int number) {
    (void)number;
    showRequested = 1;
}

void onPause(int number) {
    (void)number;
    pauseRequested = 1;
}

void closeFd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

int installSignals() {
    struct sigaction action;
    hostStopping = 0;
    showRequested = 0;
    pauseRequested = 0;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = onStop;
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) {
        return -1;
    }
    action.sa_handler = onShow;
    if (sigaction(SIGUSR1, &action, NULL)) {
        return -1;
    }
    action.sa_handler = onPause;
    if (sigaction(SIGUSR2, &action, NULL)) {
        return -1;
    }
    action.sa_handler = SIG_IGN;
    return (sigaction(SIGHUP, &action, NULL) || sigaction(SIGPIPE, &action, NULL)) ? -1 : 0;
}

class AppHostSupervisor {
public:
    AppHostSupervisor(nbt_fs *fs, const char *app, bool automatic)
        : fs_(fs), app_(app), automatic_(automatic), confirmed_(!automatic), faultCleared_(false),
          hostLock_(-1), statusFd_(-1), surfaceFd_(-1), canShow_(false), forwardsShow_(false),
          canStatus_(false), advertised_(false), statusAdvertised_(false), output_(fs, app),
          child_() {
        child_.ready_fd = -1;
        child_.lease_fd = -1;
    }

    int run() {
        if (!fs_ || !nbt::process::HostControl::validApp(app_) || installSignals()) {
            return 1;
        }
        if (!output_.start()) {
            nbt_app_output_to_log(fs_); // its messages then go to the debug log, while on
        }
        const int startCode = acquireAndSpawn();
        if (startCode >= 0) {
            return startCode;
        }
        const int result = superviseLoop();
        shutdown(result);
        output_.finishReport(); // once it no longer counts as running: sloginfo takes seconds
        return exitCode_;
    }

private:
    /* `error` is the failed call's errno, or 0 when the cause is no system
     * call (a refusal, or a check that sets none): nothing stale is logged. */
    int failStart(int txLock, const char *reason, int error) {
        closeFd(statusFd_);
        closeFd(surfaceFd_);
        bridge_.stop();
        channels_.stop();
        closeFd(txLock);
        closeFd(hostLock_);
        if (error) {
            nbt_logf("host %s: %s (errno %d)", app_, reason, error);
            output_.log().note("%s (error %d)", reason, error);
        } else {
            nbt_logf("host %s: %s", app_, reason);
            output_.log().note("%s", reason);
        }
        return 1;
    }

    bool advertiseCapability(bool enabled, off_t byte, bool &advertised) {
        if (!enabled || !child_.ready || advertised) {
            return true;
        }
        if (nbt::process::HostControl::setByteLock(hostLock_, F_RDLCK, byte)) {
            return false;
        }
        advertised = true;
        return true;
    }

    int acquireAndSpawn() {
        nbt::process::HostStart start;
        char reason[200];
        int error = 0;

        hostLock_ = nbt_fs_open(fs_, NBT_FS_HOST_LOCK, app_, NULL, NBT_FS_OPEN_LOCK);
        if (hostLock_ < 0) {
            return 1;
        }
        if (nbt::process::HostControl::setByteLock(hostLock_, F_WRLCK, 0)) {
            close(hostLock_);
            return (errno == EACCES || errno == EAGAIN) ? 3 : 1;
        }
        errno = 0;
        const int txLock = nbt_tx_lock(fs_, app_);
        if (txLock < 0) {
            return failStart(-1, "update in progress", errno);
        }
        errno = 0;
        const int lease = nbt_fs_lease_exclusive(fs_, app_);
        if (lease < 0) {
            return failStart(txLock, "already running", errno);
        }
        if (automatic_) {
            const int autoRes = nbt_autostart_begin(fs_, app_);
            if (autoRes) {
                close(lease);
                (void)failStart(txLock, "autostart disabled, guarded or unavailable", 0);
                return autoRes > 0 ? 0 : 1;
            }
        }
        if (nbt::process::prepareHostStart(fs_, app_, lease, &start, reason, sizeof(reason), &error,
                                           channels_)) {
            return failStart(txLock, reason, error);
        }
        canShow_ = start.canShow;
        // An application that places its own surface and asks for the focus
        // hold comes back on a show request too (selected in the manager,
        // the App shortcut): SIGUSR1, as for `show` (docs/app-bridge.md).
        forwardsShow_ = canShow_ || nbt_bridge_cap_has(start.grantCaps, "hold");
        canStatus_ = start.canStatus;
        if (canStatus_ && (statusFd_ = nbt_fs_status_open(fs_, app_, 1)) < 0) {
            nbt_logf("host %s: no status channel (errno %d); started without", app_, errno);
            canStatus_ = false;
            nbt_app_environment_remove(&start.environment, "NBT_STATUS_FD=5");
        }
        bridge_.prepare(fs_, app_, start.grantCaps, start.record.bridge, &start.environment);
        errno = 0;
        const nbt_app_channels appChannels = {statusFd_, channels_.most(), channels_.gamepad(),
                                              channels_.controller(), bridge_.holdFd()};
        const char *failed =
            canShow_ && nbt_app_surface_grant(fs_, app_, &start.environment, &surfaceFd_)
                ? "cannot start: environment full"
            : nbt_process_start_host_channels(fs_, app_, start.environment.list, &appChannels,
                                              &child_, start.nowMs)
                ? "cannot start the application"
                : NULL;
        if (failed) {
            return failStart(txLock, failed, errno);
        }
        channels_.spawned(surfaceFd_ >= 0);
        close(txLock);
        nbt_logf("host %s: started %s (pid %ld)", app_, start.record.version,
                 static_cast<long>(child_.pid));
        output_.beginRun(start.record.version, start.record.sequence, static_cast<long>(child_.pid),
                         automatic_ ? "autostart" : "by hand");
        output_.log().note("started %s (pid %ld)", start.record.version,
                           static_cast<long>(child_.pid));
        return -1;
    }

    int superviseLoop() {
        uint64_t now = 0;
        while (!hostStopping) {
            struct pollfd ready[2] = {{channels_.wakeFd(), POLLIN, 0}, {output_.fd(), POLLIN, 0}};
            if ((poll(ready, 2, kPollMs) < 0 && errno != EINTR) || nbt_monotonic_ms(&now)) {
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
            if (hostStopping) {
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
            const bool pauseNow = pauseRequested != 0;
            pauseRequested = 0;
            const int step = pause_.service(app_, child_, channels_, output_.log(), now, pauseNow,
                                            showRequested != 0);
            if (step) {
                hostStopping = step > 0; // paused for 2 minutes
                return step > 0 ? 0 : 1;
            }
            if (showRequested) {
                showRequested = 0;
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

    void shutdown(int result) {
        exitCode_ = result;
        if ((advertised_ && nbt::process::HostControl::setByteLock(hostLock_, F_UNLCK, 1)) ||
            (statusAdvertised_ && nbt::process::HostControl::setByteLock(hostLock_, F_UNLCK, 3))) {
            exitCode_ = 1;
        }
        closeFd(statusFd_);
        if (child_.pid > 0) {
            if (hostStopping) {
                nbt_logf("host %s: stopping", app_);
            }
            pause_.beforeStop(child_);
            output_.stopChild(child_);
            output_.drain();
            if (hostStopping) {
                output_.log().note("stopped by the manager");
            }
        }
        if (result != 0 || (!hostStopping && !faultCleared_ &&
                            (child_.fault || child_.exit_signal || child_.exit_status))) {
            (void)nbt_app_fault_record(fs_, app_);
        }
        bridge_.stop();
        channels_.stop();
        nbt_logf("host %s: ended", app_);
        closeFd(surfaceFd_);
        closeFd(hostLock_);
    }

    nbt_fs *fs_;
    const char *app_;
    bool automatic_, confirmed_, faultCleared_;
    int hostLock_, statusFd_, surfaceFd_;
    bool canShow_, forwardsShow_, canStatus_, advertised_, statusAdvertised_;
    nbt::process::AppOutput output_;
    nbt::process::HostChannels channels_;
    nbt::process::HostBridge bridge_;
    nbt::process::AppPause pause_;
    int exitCode_ = 0;
    nbt_process child_;
};

} // namespace

int nbt_app_host_run_mode(nbt_fs *fs, const char *app, int automatic) {
    AppHostSupervisor supervisor(fs, app, automatic != 0);
    return supervisor.run();
}

int nbt_app_host_run(nbt_fs *fs, const char *app) {
    return nbt_app_host_run_mode(fs, app, 0);
}
