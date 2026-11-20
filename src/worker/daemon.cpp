// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/daemon.hpp"

#include "nbt/app_compat.h"
#include "nbt/app_host.h"
#include "nbt/autostart.h"
#include "nbt/core/system.hpp"
#include "nbt/crypto/threads.hpp"
#include "nbt/firewall.h"
#include "nbt/fs.h"
#include "nbt/log.h"
#include "nbt/process/sandbox_limits.hpp"
#include "nbt/worker/storage_report.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

const char *stateName(State state) {
    static const char *const names[] = {"idle",       "session",   "verified",
                                        "installing", "installed", "failed"};
    const unsigned index = static_cast<unsigned>(state);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}

} // namespace

Daemon::Daemon(int controlFd)
    : root_(), children_(), hosts_(), worker_(root_, children_, hosts_), web_(worker_), wfd_(),
      wfdJob_(wfd_), controlOwner_(controlFd), control_(controlFd),
      dispatcher_(root_, worker_, web_, wfdJob_, control_), loggedState_(State::Idle),
      loggedOpened_(false), loggedMessage_() {
    loggedMessage_[0] = 0;
}

core::Status Daemon::initialize(const char *root, const char *self) {
    if (fcntl(controlOwner_.get(), F_SETFD, FD_CLOEXEC) < 0) {
        return core::Status::fromErrno("cannot configure control descriptor");
    }
    core::Status status = root_.initialize(root);
    if (!status.ok()) {
        return status;
    }
    if (!web_.initialize().ok()) {
        return core::Status::error(EIO, "phone API initialization failed");
    }
    // Before any thread: verification and copies run on the worker's job.
    if (!crypto::initializeThreads() || !worker_.initializeJobs()) {
        return core::Status::error(ENOMEM, "worker job setup failed");
    }
    wfdJob_.initialize();
    // No manager lock of its own: the launcher, the worker's only parent,
    // holds the manager operation lock for the worker's whole life (setup,
    // staging and provisioning stay out) and must itself write the manager
    // state meanwhile, to confirm a trial after 30 healthy seconds.
#ifdef NBT_TESTING
    (void)nbt_app_host_set_self(self, root);
#else
    (void)nbt_app_host_set_self(self, NULL);
#endif
    if (nbt_log_open(root_.legacy(), "worker") > 0) {
        nbt_logf("start");
        logStorage(root);
    }
    const int removed = nbt_firewall_close();
    if (removed > 0) {
        nbt_logf("firewall: removed %d session rule(s) left by an earlier worker", removed);
    }
    return core::Status();
}

void Daemon::logChanges() {
    const WorkerState &state = worker_.state();
    if (state.state == loggedState_ && !strcmp(state.message, loggedMessage_) &&
        state.session.opened() == loggedOpened_) {
        return;
    }
    if (state.session.opened() && !loggedOpened_) {
        nbt_logf("upload page opened");
    }
    if (state.state != loggedState_ || strcmp(state.message, loggedMessage_)) {
        nbt_logf("state %s: %s", stateName(state.state), state.message[0] ? state.message : "-");
    }
    loggedState_ = state.state;
    loggedOpened_ = state.session.opened();
    snprintf(loggedMessage_, sizeof(loggedMessage_), "%s", state.message);
}

core::Status Daemon::command(char *line, uint64_t nowMs) {
    return dispatcher_.dispatch(line, nowMs);
}

int Daemon::run() {
    int result = 1;
    uint64_t nextListen = 0;
    while (!control_.quitting()) {
        core::Result<uint64_t> now = core::monotonicMs();
        if (!now.get()) {
            break;
        }
        worker_.poll(now.value());
        wfdJob_.poll(now.value());
        if (wfdJob_.enabledByUs()) {
            const State st = worker_.state().state;
            if (st != State::Session && st != State::Verified && st != State::Installing &&
                st != State::Installed) {
                wfdJob_.restore(now.value());
            }
        }
        // Not while SESSION waits for Wi-Fi Direct: it listens on its own interface after.
        if (!web_.listening() && !dispatcher_.replyPending() && now.value() >= nextListen) {
            (void)web_.listen("wfd0", 8080);
            nextListen = now.value() + 1000;
        }
        logChanges();
        fd_set readers;
        fd_set writers;
        FD_ZERO(&readers);
        FD_ZERO(&writers);
        FD_SET(controlOwner_.get(), &readers);
        int highest = controlOwner_.get();
        web_.sets(&readers, &writers, &highest);
        wfdJob_.sets(&readers, &highest);
        worker_.sets(&readers, &highest);
        struct timeval timeout = {0, 200000};
        const int selected = select(highest + 1, &readers, &writers, NULL, &timeout);
        if (selected < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        now = core::monotonicMs();
        if (!now.get()) {
            break;
        }
        if (wfdJob_.fd() >= 0 && FD_ISSET(wfdJob_.fd(), &readers)) {
            wfdJob_.poll(now.value());
        }
        // A pending SESSION reply, then the commands that waited for it.
        if (dispatcher_.replyPending() &&
            (!dispatcher_.poll(now.value()).ok() ||
             (!dispatcher_.replyPending() && !control_.process(*this, now.value()).ok()))) {
            break;
        }
        if (FD_ISSET(controlOwner_.get(), &readers) && !control_.read(*this, now.value()).ok()) {
            break;
        }
        web_.handle(worker_, &readers, &writers, now.value());
        logChanges();
    }
    if (control_.quitting()) {
        result = 0;
    }
    nbt_logf("exit (%s)", control_.quitting() ? "quit" : "control channel closed");
    web_.close(worker_);
    wfdJob_.shutdown();
    worker_.shutdown();
    return result;
}

int hostMain(const char *root, int argc, char **argv) {
    storage::ManagedRoot rootObject;
#ifdef NBT_TESTING
    if (argc == 5 && !strcmp(argv[3], "--root")) {
        root = argv[4];
    } else if (argc != 3) {
        return 2;
    }
#else
    if (argc != 3) {
        return 2;
    }
#endif
    if (!rootObject.initialize(root).ok()) {
        return 1;
    }
    (void)nbt_log_open(rootObject.legacy(), "host");
    process::setAppExecWrapper(argv[0]); // the car's limits wrapper is this binary
#ifdef __QNX__
    if (!process::appExecWrapper()) {
        nbt_logf("host %s: no absolute path to itself; the application starts without limits",
                 argv[2]);
    }
#endif
    const int result =
        nbt_app_host_run_mode(rootObject.legacy(), argv[2], !strcmp(argv[1], "--host-auto"));
    nbt_log_close();
    return result;
}

int autostartMain(const char *root, const char *self, int argc, char **argv) {
    (void)argv;
#ifdef NBT_TESTING
    if (argc == 4 && !strcmp(argv[2], "--root")) {
        root = argv[3];
    } else if (argc != 2) {
        return 2;
    }
#else
    if (argc != 2) {
        return 2;
    }
#endif
    storage::ManagedRoot rootObject;
    if (!rootObject.initialize(root).ok()) {
        return 1;
    }
#ifdef NBT_TESTING
    if (nbt_app_host_set_self(self, root)) {
        return 1;
    }
#else
    if (nbt_app_host_set_self(self, NULL)) {
        return 1;
    }
#endif
    (void)nbt_log_open(rootObject.legacy(), "autostart");
    char names[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1];
    const int count = nbt_fs_list_apps(rootObject.legacy(), names);
    int result = count < 0 ? 1 : 0;
    for (int i = 0; i < count; ++i) {
        nbt_autostart state;
        if (nbt_autostart_load(rootObject.legacy(), names[i], &state)) {
            result = 1;
            continue;
        }
        char reason[128];
        if (state.enabled && state.failures < NBT_AUTOSTART_FAILURE_LIMIT &&
            nbt_app_compatible(rootObject.legacy(), names[i], reason, sizeof(reason)) == 1) {
            // Disabled until updated: not a failed start, so autostart is not paused.
            nbt_logf("autostart %s: skipped, needs an update: %s", names[i], reason);
        } else if (state.enabled && state.failures < NBT_AUTOSTART_FAILURE_LIMIT) {
            const pid_t running = nbt_app_host_pid(rootObject.legacy(), names[i]);
            if (running < 0 ||
                (!running && nbt_app_host_autostart(rootObject.legacy(), names[i]))) {
                result = 1;
            }
        }
    }
    nbt_log_close();
    return result;
}

} // namespace worker
} // namespace nbt
