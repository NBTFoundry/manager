// SPDX-License-Identifier: MPL-2.0
#include "app_host_supervisor.hpp"

#include "nbt/app_faults.h"
#include "nbt/app_host.h"
#include "nbt/autostart.h"
#include "nbt/log.h"
#include "nbt/process/audio_mixer.hpp"
#include "nbt/process/host_control.hpp"
#include "nbt/process/host_signals.hpp"
#include "nbt/process/host_start.hpp"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {
// A service that ends on its own starts again after these (docs/service-bridge.md).
const unsigned kRestartSeconds[kServiceRestarts] = {1, 5, 30};
} // namespace

void closeHostFd(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

int AppHostSupervisor::run() {
    if (!fs_ || !HostControl::validApp(app_) || installHostSignals()) {
        return 1;
    }
    if (!output_.start()) {
        nbt_app_output_to_log(fs_); // its messages then go to the debug log, while on
    }
    const int startCode = lockHost() >= 0 ? exitCode_ : spawnChild();
    if (startCode >= 0) {
        return startCode;
    }
    int result = superviseLoop();
    while (service_ && !hostStopping() && restarts_ < kServiceRestarts) {
        const unsigned seconds = kRestartSeconds[restarts_++];
        endRun(result);
        nbt_logf("host %s: service ended; again in %u s (%u of %d)", app_, seconds, restarts_,
                 kServiceRestarts);
        output_.log().note("service ended; starting it again in %u s", seconds);
        if (!pauseFor(seconds * 1000u)) {
            break;
        }
        const int code = spawnChild();
        if (code >= 0) {
            return code;
        }
        result = superviseLoop();
    }
    if (service_ && !hostStopping()) {
        output_.log().note("service ended %d times; stopped until its application starts",
                           kServiceRestarts);
    }
    shutdown(result);
    output_.finishReport(); // once it no longer counts as running: sloginfo takes seconds
    return exitCode_;
}

/* `error` is the failed call's errno, or 0 when the cause is no system
 * call (a refusal, or a check that sets none): nothing stale is logged. */
int AppHostSupervisor::failStart(int txLock, const char *reason, int error) {
    closeHostFd(statusFd_);
    closeHostFd(surfaceFd_);
    bridge_.stop();
    channels_.stop();
    closeHostFd(txLock);
    closeHostFd(hostLock_);
    if (error) {
        nbt_logf("host %s: %s (errno %d)", app_, reason, error);
        output_.log().note("%s (error %d)", reason, error);
    } else {
        nbt_logf("host %s: %s", app_, reason);
        output_.log().note("%s", reason);
    }
    return 1;
}

/* -1 once this host holds its lock, else its exit code in exitCode_: 3
 * when another host runs. */
int AppHostSupervisor::lockHost() {
    hostLock_ = nbt_fs_open(fs_, service_ ? NBT_FS_SERVICE_LOCK : NBT_FS_HOST_LOCK, app_, NULL,
                            NBT_FS_OPEN_LOCK);
    exitCode_ = 1;
    if (hostLock_ >= 0 && HostControl::setByteLock(hostLock_, F_WRLCK, 0)) {
        exitCode_ = (errno == EACCES || errno == EAGAIN) ? 3 : 1;
        closeHostFd(hostLock_);
    }
    return hostLock_ >= 0 ? -1 : exitCode_;
}

int AppHostSupervisor::spawnChild() {
    HostStart start;
    char reason[200];
    int error = 0;
    errno = 0;
    const int txLock = nbt_tx_lock(fs_, app_);
    if (txLock < 0) {
        return failStart(-1, "update in progress", errno);
    }
    errno = 0;
    int lease =
        service_ ? nbt_fs_service_lease_exclusive(fs_, app_) : nbt_fs_lease_exclusive(fs_, app_);
    if (lease < 0) {
        return failStart(txLock, "already running", errno);
    }
    if (automatic_ && !restarts_) {
        const int autoRes = nbt_autostart_begin(fs_, app_);
        if (autoRes) {
            close(lease);
            (void)failStart(txLock, "autostart disabled, guarded or unavailable", 0);
            return autoRes > 0 ? 0 : 1;
        }
    }
    // A service recovers an interrupted update only while its application is not running.
    int recovery = lease;
    if (service_) {
        recovery = nbt_fs_lease_exclusive(fs_, app_);
        closeHostFd(lease); // free again for the service process
    }
    if (prepareHostStart(fs_, app_, recovery, &start, reason, sizeof(reason), &error, channels_,
                         service_)) {
        return failStart(txLock, reason, error);
    }
    canShow_ = start.canShow;
    // An application with its own surface and the focus hold comes back on a
    // show request too (the manager, App shortcut): SIGUSR1 (docs/app-bridge.md).
    forwardsShow_ = canShow_ || nbt_bridge_cap_has(start.grantCaps, "hold");
    canStatus_ = start.canStatus;
    if (canStatus_ && (statusFd_ = nbt_fs_status_open(fs_, app_, 1)) < 0) {
        nbt_logf("host %s: no status channel (errno %d); started without", app_, errno);
        canStatus_ = false;
        nbt_app_environment_remove(&start.environment, "NBT_STATUS_FD=5");
    }
    bridge_.prepare(fs_, app_, start.grantCaps, start.record.bridge, &start.environment);
    // Every application's and service's sound through the mixer (docs/audio-mixer.md).
    (void)grantMixer(&start.environment);
    errno = 0;
    const nbt_app_channels appChannels = {
        statusFd_,        channels_.most(), channels_.gamepad(), channels_.controller(),
        bridge_.holdFd(), bridge_.diagFd(), service_ ? 1 : 0};
    const char *failed =
        canShow_ && nbt_app_surface_grant(fs_, app_, &start.environment, &surfaceFd_)
            ? "cannot start: environment full"
        : nbt_process_start_host_channels(fs_, app_, start.environment.list, &appChannels, &child_,
                                          start.nowMs)
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
                     restarts_    ? "restart"
                     : automatic_ ? "autostart"
                                  : "by hand");
    output_.log().note("started %s (pid %ld)", start.record.version, static_cast<long>(child_.pid));
    if (!service_ && nbt_bridge_cap_has(start.grantCaps, "service") &&
        nbt_app_service_start(fs_, app_, automatic_)) {
        nbt_logf("host %s: cannot start its service (errno %d)", app_, errno);
    }
    return -1;
}

} // namespace process
} // namespace nbt

int nbt_app_host_run_mode(nbt_fs *fs, const char *app, int automatic) {
    nbt::process::AppHostSupervisor supervisor(fs, app, automatic != 0, false);
    return supervisor.run();
}

int nbt_app_service_run(nbt_fs *fs, const char *app, int automatic) {
    nbt::process::AppHostSupervisor supervisor(fs, app, automatic != 0, true);
    return supervisor.run();
}

int nbt_app_host_run(nbt_fs *fs, const char *app) {
    return nbt_app_host_run_mode(fs, app, 0);
}
