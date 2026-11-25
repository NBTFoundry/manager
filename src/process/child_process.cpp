// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/child_process.hpp"

#include "nbt/process.h"
#include "nbt/process/child_spawn.hpp"
#include "nbt/process/process_group.hpp"

#include <errno.h>
#include <string.h>
#include <utility>

extern "C" char **environ;

using nbt::process::ProcessGroup;

int nbt_process_start(nbt_fs *fs, const char *app, nbt_process *child, uint64_t now_ms) {
    return ProcessGroup::start(fs, app, NBT_FS_ACTIVE, NULL, NULL, -1, -1, child, now_ms);
}

int nbt_process_start_env(nbt_fs *fs, const char *app, char *const *environment, nbt_process *child,
                          uint64_t now_ms) {
    return ProcessGroup::start(fs, app, NBT_FS_ACTIVE, NULL, environment, -1, -1, child, now_ms);
}

int nbt_process_start_host(nbt_fs *fs, const char *app, char *const *environment, int status_fd,
                           int most_fd, nbt_process *child, uint64_t now_ms) {
    return ProcessGroup::start(fs, app, NBT_FS_ACTIVE, NULL, environment, status_fd, most_fd, child,
                               now_ms);
}

int nbt_process_start_host_channels(nbt_fs *fs, const char *app, char *const *environment,
                                    const nbt_app_channels *c, nbt_process *child,
                                    uint64_t now_ms) {
    if (!c) {
        errno = EINVAL;
        return -1;
    }
    return ProcessGroup::start(fs, app, NBT_FS_ACTIVE, NULL, environment, c->status, c->most, child,
                               now_ms, c->gamepad, c->controller, c->hold, c->diag);
}

int nbt_process_start_manager(nbt_fs *fs, unsigned slot, int worker_fd, nbt_process *child,
                              uint64_t now_ms) {
    if (slot > 1 || worker_fd < 0) {
        errno = EINVAL;
        return -1;
    }
    const char *number = slot ? "1" : "0";
    // The GUI gets the launcher's environment: the one the plugin builds as NBT-AAD does.
    return ProcessGroup::start(fs, "manager", NBT_FS_MANAGER_SLOT_GUI, number, environ, worker_fd,
                               -1, child, now_ms);
}

int nbt_process_poll(nbt_process *child, uint64_t now_ms) {
    return ProcessGroup::poll(child, now_ms);
}

int nbt_process_healthy(const nbt_process *child, uint64_t now_ms) {
    return ProcessGroup::healthy(child, now_ms);
}

int nbt_process_signal(const nbt_process *child, int signal_number) {
    return ProcessGroup::signal(child, signal_number);
}

int nbt_process_stop(nbt_process *child) {
    return ProcessGroup::stop(child);
}

int nbt_process_terminate(nbt_process *child, uint64_t now_ms) {
    return ProcessGroup::terminate(child, now_ms);
}

int nbt_process_reap(nbt_process *child, uint64_t now_ms) {
    return ProcessGroup::reap(child, now_ms);
}

namespace nbt {
namespace process {
namespace {

void resetChild(nbt_process *child) {
    memset(child, 0, sizeof(*child));
    child->ready_fd = -1;
    child->lease_fd = -1;
}

core::Status failed(const char *message) {
    return core::Status::error(errno ? errno : EIO, message);
}

} // namespace

ChildProcess::ChildProcess()
    : child_()
#ifdef NBT_TESTING
      ,
      testOnly_(false)
#endif
{
    resetChild(&child_);
}

#ifdef NBT_TESTING
ChildProcess ChildProcess::forTesting(pid_t pid, uint64_t readyMs) {
    ChildProcess process;
    process.child_.pid = pid;
    process.child_.ready = 1;
    process.child_.ready_ms = readyMs;
    process.testOnly_ = true;
    return process;
}

void ChildProcess::setStoppingForTesting(uint64_t nowMs) {
    child_.stopping = 1;
    child_.stop_ms = nowMs;
}

void ChildProcess::resetForTesting() {
    resetChild(&child_);
}
#endif

ChildProcess::~ChildProcess() {
#ifdef NBT_TESTING
    if (testOnly_) {
        return;
    }
#endif
    if (child_.pid > 0) {
        (void)nbt_process_stop(&child_);
    }
}

ChildProcess::ChildProcess(ChildProcess &&other)
    : child_(other.child_)
#ifdef NBT_TESTING
      ,
      testOnly_(other.testOnly_)
#endif
{
    resetChild(&other.child_);
#ifdef NBT_TESTING
    other.testOnly_ = false;
#endif
}

ChildProcess &ChildProcess::operator=(ChildProcess &&other) {
    if (this == &other) {
        return *this;
    }
#ifdef NBT_TESTING
    if (!testOnly_ && child_.pid > 0) {
        (void)nbt_process_stop(&child_);
    }
    testOnly_ = other.testOnly_;
    other.testOnly_ = false;
#else
    if (child_.pid > 0) {
        (void)nbt_process_stop(&child_);
    }
#endif
    child_ = other.child_;
    resetChild(&other.child_);
    return *this;
}

core::Result<ChildProcess> ChildProcess::start(const storage::ManagedRoot &root, const char *app,
                                               char *const *environment, int statusFd, int mostFd,
                                               int outputFd, uint64_t nowMs) {
    ChildProcess process;
    const ChildSandbox sandbox = defaultAppSandbox();
    if (!root.valid() || ProcessGroup::start(const_cast<nbt_fs *>(root.legacy()), app,
                                             NBT_FS_ACTIVE, NULL, environment, statusFd, mostFd,
                                             outputFd, &sandbox, &process.child_, nowMs)) {
        return core::Result<ChildProcess>::failure(failed("application process start failed"));
    }
    return core::Result<ChildProcess>::success(std::move(process));
}

core::Result<ChildProcess> ChildProcess::startManager(const storage::ManagedRoot &root,
                                                      unsigned slot, int workerFd, uint64_t nowMs) {
    ChildProcess process;
    if (!root.valid() || nbt_process_start_manager(const_cast<nbt_fs *>(root.legacy()), slot,
                                                   workerFd, &process.child_, nowMs)) {
        return core::Result<ChildProcess>::failure(failed("manager process start failed"));
    }
    return core::Result<ChildProcess>::success(std::move(process));
}

core::Result<bool> ChildProcess::poll(uint64_t nowMs) {
    const int result = nbt_process_poll(&child_, nowMs);
    if (result < 0) {
        return core::Result<bool>::failure(failed("application contract failed"));
    }
    return core::Result<bool>::success(result == 1);
}

bool ChildProcess::healthy(uint64_t nowMs) const {
    return nbt_process_healthy(&child_, nowMs) == 1;
}

core::Status ChildProcess::stop() {
    if (nbt_process_stop(&child_)) {
        return failed("application process stop failed");
    }
    return core::Status();
}

core::Status ChildProcess::terminate(uint64_t nowMs) {
    if (nbt_process_terminate(&child_, nowMs)) {
        return failed("application process terminate failed");
    }
    return core::Status();
}

core::Result<bool> ChildProcess::reap(uint64_t nowMs) {
    const int result = nbt_process_reap(&child_, nowMs);
    if (result < 0) {
        return core::Result<bool>::failure(failed("application process reap failed"));
    }
    return core::Result<bool>::success(result == 1);
}

core::Result<ChildProcess> RealChildRunner::start(const storage::ManagedRoot &root, const char *app,
                                                  char *const *environment, int outputFd,
                                                  uint64_t nowMs) {
    return ChildProcess::start(root, app, environment, -1, -1, outputFd, nowMs);
}

core::Result<bool> RealChildRunner::poll(ChildProcess &child, uint64_t nowMs) {
    return child.poll(nowMs);
}

bool RealChildRunner::healthy(const ChildProcess &child, uint64_t nowMs) {
    return child.healthy(nowMs);
}

core::Status RealChildRunner::stop(ChildProcess &child) {
    return child.stop();
}

core::Status RealChildRunner::terminate(ChildProcess &child, uint64_t nowMs) {
    return child.terminate(nowMs);
}

core::Result<bool> RealChildRunner::reap(ChildProcess &child, uint64_t nowMs) {
    return child.reap(nowMs);
}

} // namespace process
} // namespace nbt
