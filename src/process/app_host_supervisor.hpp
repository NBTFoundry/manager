// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_APP_HOST_SUPERVISOR_HPP
#define NBT_PROCESS_APP_HOST_SUPERVISOR_HPP

/* An application's host (src/process/app_host.cpp starts it, app_host_run.cpp
 * supervises it): one for the application, one for its service
 * (docs/service-bridge.md). Private to src/process. */

#include "nbt/fs.h"
#include "nbt/process.h"
#include "nbt/process/app_output.hpp"
#include "nbt/process/app_pause.hpp"
#include "nbt/process/host_bridge.hpp"
#include "nbt/process/host_channels.hpp"

#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace process {

enum { kHostPollMs = 200, kServiceRestarts = 3 };

void closeHostFd(int &fd);

class AppHostSupervisor {
public:
    AppHostSupervisor(nbt_fs *fs, const char *app, bool automatic, bool service)
        : fs_(fs), app_(app), automatic_(automatic), service_(service), confirmed_(!automatic),
          faultCleared_(service), hostLock_(-1), statusFd_(-1), surfaceFd_(-1), canShow_(false),
          forwardsShow_(false), canStatus_(false), advertised_(false), statusAdvertised_(false),
          restarts_(0), output_(fs, app, service), child_() {
        child_.ready_fd = -1;
        child_.lease_fd = -1;
    }

    int run();

private:
    /* `error` is the failed call's errno, or 0 when the cause is no system
     * call (a refusal, or a check that sets none): nothing stale is logged. */
    int failStart(int txLock, const char *reason, int error);
    bool advertiseCapability(bool enabled, off_t byte, bool &advertised);
    /* -1 once this host holds its lock, else its exit code in exitCode_: 3
     * when another host runs. */
    int lockHost();
    int spawnChild();
    // A service's run ended: what the next one prepares again is released.
    void endRun(int result);
    // Waits `ms`, copying the output; false when stopped meanwhile.
    bool pauseFor(uint64_t ms);
    int superviseLoop();
    void shutdown(int result);

    nbt_fs *fs_;
    const char *app_;
    bool automatic_, service_, confirmed_, faultCleared_;
    int hostLock_, statusFd_, surfaceFd_;
    bool canShow_, forwardsShow_, canStatus_, advertised_, statusAdvertised_;
    unsigned restarts_;
    AppOutput output_;
    HostChannels channels_;
    HostBridge bridge_;
    AppPause pause_;
    int exitCode_ = 0;
    nbt_process child_;
};

} // namespace process
} // namespace nbt

#endif
