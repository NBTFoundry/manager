// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/manager_worker.hpp"

#include "nbt/fs.h"
#include "nbt/log.h"
#include "nbt/process/child_spawn.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern "C" char **environ;

namespace nbt {
namespace launcher {
namespace {

const unsigned kStopWaitMs = 6000;

core::Status failed(const char *message) {
    return core::Status::error(errno ? errno : EIO, message);
}

} // namespace

ManagerWorker::ManagerWorker() : pid_(0) {
    exitReason_[0] = 0;
}

ManagerWorker::~ManagerWorker() {
    stop();
}

core::Result<core::UniqueFd> ManagerWorker::start(const storage::ManagedRoot &root, unsigned slot) {
    char path[NBT_PATH_MAX];
    char *argv[6] = {path, const_cast<char *>("--control-fd"), const_cast<char *>("3"), NULL, NULL,
                     NULL};
    const char *executable = path;
    if (pid_ > 0 || slot > 1 || !root.valid() ||
        nbt_fs_path(const_cast<nbt_fs *>(root.legacy()), NBT_FS_MANAGER_SLOT_WORKER, "manager",
                    slot ? "1" : "0", path, sizeof(path))) {
        return core::Result<core::UniqueFd>::failure(failed("manager worker path"));
    }
#ifdef NBT_TESTING
    if (getenv("NBT_TEST_MANAGER_WORKER") && getenv("NBT_TEST_MANAGER_ROOT")) {
        executable = argv[0] = getenv("NBT_TEST_MANAGER_WORKER");
        argv[3] = const_cast<char *>("--root");
        argv[4] = getenv("NBT_TEST_MANAGER_ROOT");
    }
#endif
    int ends[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends)) {
        return core::Result<core::UniqueFd>::failure(failed("manager worker channel"));
    }
    // Both ends stay private to the launcher; each child gets its own copy.
    core::UniqueFd workerEnd(ends[0]), guiEnd(ends[1]);
    core::UniqueFd copy(fcntl(workerEnd.get(), F_DUPFD, 10));
    if (fcntl(workerEnd.get(), F_SETFD, FD_CLOEXEC) || fcntl(guiEnd.get(), F_SETFD, FD_CLOEXEC) ||
        !copy.valid()) {
        return core::Result<core::UniqueFd>::failure(failed("manager worker channel"));
    }
    const int fds[4] = {0, 1, 2, copy.get()};
    pid_t pid = 0;
    const int error = process::spawnChild(executable, argv, environ, fds, 4, &pid);
    if (error) {
        errno = error;
        nbt_logf("worker %s: spawn failed (%s)", executable, strerror(error));
        return core::Result<core::UniqueFd>::failure(failed("manager worker start failed"));
    }
    pid_ = pid;
    exitReason_[0] = 0;
    nbt_logf("worker started, pid %d", static_cast<int>(pid));
    return core::Result<core::UniqueFd>::success(std::move(guiEnd));
}

void ManagerWorker::reaped(int status) {
    if (WIFSIGNALED(status)) {
        snprintf(exitReason_, sizeof(exitReason_), "terminated by signal %d", WTERMSIG(status));
    } else {
        snprintf(exitReason_, sizeof(exitReason_), "exited with status %d",
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }
    nbt_logf("worker %s", exitReason_);
    pid_ = 0;
}

bool ManagerWorker::running() {
    if (pid_ <= 0) {
        return false;
    }
    int status = 0;
    pid_t result;
    do {
        result = waitpid(pid_, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == pid_) {
        reaped(status);
        return false;
    }
    if (result < 0) {
        snprintf(exitReason_, sizeof(exitReason_), "lost (errno %d)", errno);
        nbt_logf("worker %s", exitReason_);
        pid_ = 0;
        return false;
    }
    return true;
}

void ManagerWorker::stop() {
    if (pid_ <= 0) {
        return;
    }
    const struct timespec pause = {0, 100000000};
    for (unsigned waited = 0; waited < kStopWaitMs; waited += 100) {
        if (!running()) {
            return;
        }
        nanosleep(&pause, NULL);
    }
    nbt_logf("worker still running %u ms after the GUI; killing it", kStopWaitMs);
    if (kill(pid_, SIGKILL) && errno != ESRCH) {
        nbt_logf("worker kill failed (errno %d)", errno);
        pid_ = 0;
        return;
    }
    int status = 0;
    while (waitpid(pid_, &status, 0) < 0) {
        if (errno != EINTR) {
            pid_ = 0;
            return;
        }
    }
    reaped(status);
}

} // namespace launcher
} // namespace nbt
