// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_APP_PAUSE_HPP
#define NBT_PROCESS_APP_PAUSE_HPP

// An application paused by its host while iDrive has the screen: the manager
// GUI asks (SIGUSR2 to the host) when iDrive takes over from an application
// it showed (NAV, MENU, OPTION, a call, ...). The host stops the application's
// process group (SIGSTOP), stops relaying MOST messages and gamepad input to
// it, and holds its heartbeat clock (a stopped process sends none). A show
// request continues it; after kPausedCloseMs paused the host stops it.

#include "nbt/log/app_log.hpp"
#include "nbt/process.h"

#include <stdint.h>

namespace nbt {
namespace process {

class HostChannels;

static const uint64_t kPausedCloseMs = 120000u;

class AppPause {
public:
    explicit AppPause(uint64_t closeMs = kPausedCloseMs);
    // Stops the child; channels may be NULL. False if the signal failed.
    bool pause(const nbt_process &child, HostChannels *channels, uint64_t nowMs);
    // Continues the child, with a fresh heartbeat window.
    bool resume(nbt_process &child, HostChannels *channels, uint64_t nowMs);
    // While paused, keeps the heartbeat check from failing the stopped child.
    void hold(nbt_process &child, uint64_t nowMs) const;
    // True once the child has been paused for closeMs.
    bool due(uint64_t nowMs) const;
    // The host loop's requests, logged: a pause, or a show that continues it.
    // -1 when a signal failed, 1 once paused for closeMs (stop it), else 0.
    int service(const char *app, nbt_process &child, HostChannels &channels, log::AppLog &log,
                uint64_t nowMs, bool pauseRequested, bool showRequested);
    // A paused child gets SIGTERM and then SIGCONT, so it handles the stop.
    void beforeStop(const nbt_process &child);
    bool paused() const {
        return paused_;
    }

private:
    uint64_t closeMs_;
    uint64_t pausedMs_;
    bool paused_;
};

} // namespace process
} // namespace nbt

#endif
