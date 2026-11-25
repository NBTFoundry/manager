// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/process_group.hpp"

#include "nbt/package/payload_note.hpp"
#include "nbt/process/child_spawn.hpp"
#include "nbt/process/mapped_channels.hpp"
#include "nbt/process/runner_path.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

void closeIfOpen(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

int signalTarget(pid_t pid, int sig) {
    // Sandboxed app children lead their own process group; kill the group too when present.
    if (getpgid(pid) == pid && kill(-pid, sig) == 0) {
        return 0;
    }
    return kill(pid, sig);
}

} // namespace

// Copies for the child stay open across exec, as the descriptors NBT-AAD maps.
int ProcessGroup::duplicatePrivate(int fd, bool closeOnExec) {
    int copy = fcntl(fd, F_DUPFD, 10);
    if (copy >= 0 && closeOnExec && fcntl(copy, F_SETFD, FD_CLOEXEC) < 0) {
        closeIfOpen(copy);
    }
    return copy;
}

void ProcessGroup::closeHandles(nbt_process *child) {
    closeIfOpen(child->ready_fd);
    closeIfOpen(child->lease_fd);
    child->pid = 0;
    child->stopping = 0;
    child->killed = 0;
}

int ProcessGroup::hasLease(const nbt_process *child) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(child->lease_fd, F_GETLK, &lock) < 0) {
        return 0;
    }
    return lock.l_type == F_RDLCK && lock.l_pid == child->pid;
}

int ProcessGroup::ended(nbt_process *child, unsigned tries) {
    const struct timespec pause = {0, 20000000};
    for (unsigned i = 0; i < tries; ++i) {
        int status = 0;
        const pid_t result = waitpid(child->pid, &status, WNOHANG);
        if (result == child->pid) {
            child->exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            child->exit_signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
            closeHandles(child);
            return 0;
        }
        if (result < 0 && errno != EINTR) {
            return -1;
        }
        if (i + 1 < tries) {
            (void)nanosleep(&pause, NULL);
        }
    }
    return 1;
}

int ProcessGroup::start(nbt_fs *fs, const char *app, nbt_fs_path_kind kind, const char *slot,
                        char *const *environment, int statusFd, int mostFd, nbt_process *child,
                        uint64_t nowMs, int gamepadFd, int controllerFd, int holdFd, int diagFd,
                        nbt_fs_path_kind leaseKind) {
    const ChildSandbox sandbox = defaultAppSandbox();
    return start(fs, app, kind, slot, environment, statusFd, mostFd, -1,
                 kind == NBT_FS_ACTIVE ? &sandbox : NULL, child, nowMs, gamepadFd, controllerFd,
                 holdFd, diagFd, leaseKind);
}

int ProcessGroup::start(nbt_fs *fs, const char *app, nbt_fs_path_kind kind, const char *slot,
                        char *const *environment, int statusFd, int mostFd, int outputFd,
                        const ChildSandbox *sandbox, nbt_process *child, uint64_t nowMs,
                        int gamepadFd, int controllerFd, int holdFd, int diagFd,
                        nbt_fs_path_kind leaseKind) {
    char path[NBT_PATH_MAX];
    char *argv[16];
    const char *executable = path;
    char *const empty[] = {NULL};
    int active = -1, lease = -1, leaseChild = -1, pipefds[2] = {-1, -1};
    int readyParent = -1, readyChild = -1;
    MappedChannels channels(statusFd, mostFd, gamepadFd, controllerFd, outputFd, holdFd, diagFd);
    pid_t pid = 0;
    if (!channels.valid() || !child || child->pid > 0 ||
        nbt_fs_path(fs, kind, app, slot, path, sizeof(path))) {
        return -1;
    }
    active = nbt_fs_open(fs, kind, app, slot, NBT_FS_READ);
    if (active < 0) {
        return -1;
    }
    {
        // Built without the QNX SDP (bridge capability runner): the release's
        // nbt-runner runs it, and checks the note's ABI before anything else.
        package::PayloadNote note;
        const char *runner = NULL;
        if (kind == NBT_FS_ACTIVE && package::readPayloadNote(active, &note)) {
            runner = runnerPath();
            if (!runner || access(runner, X_OK)) {
                closeIfOpen(active);
                errno = ENOEXEC; // this manager has no runner
                return -1;
            }
        }
        closeIfOpen(active);
        buildProcessArgv(argv, path, kind, slot, &executable, runner);
    }
    lease = nbt_fs_open(fs, leaseKind, app, NULL, NBT_FS_OPEN_LOCK);
    if (lease < 0 || pipe(pipefds)) {
        goto fail;
    }
    leaseChild = duplicatePrivate(lease, false);
    readyParent = duplicatePrivate(pipefds[0], true);
    readyChild = duplicatePrivate(pipefds[1], false);
    closeIfOpen(pipefds[0]);
    closeIfOpen(pipefds[1]);
    if (leaseChild < 0 || readyParent < 0 || readyChild < 0 ||
        fcntl(readyParent, F_SETFL, O_NONBLOCK) < 0 || fcntl(lease, F_SETFD, FD_CLOEXEC) < 0) {
        goto fail;
    }
    {
        // Standard streams as the caller's (or outputFd), then ready 3, lease 4, status 5,
        // MOST 6, gamepad 7, controller 8, hold 9, diag 10; up to the last one present.
        const int out = channels.output() >= 0 ? channels.output() : 1;
        const int err = channels.output() >= 0 ? channels.output() : 2;
        const int fds[11] = {0,
                             out,
                             err,
                             readyChild,
                             leaseChild,
                             channels.status(),
                             channels.most(),
                             channels.gamepad(),
                             channels.controller(),
                             channels.hold(),
                             channels.diag()};
        unsigned count = 11;
        while (count > 5 && fds[count - 1] < 0) {
            --count;
        }
        const int spawnError = spawnSandboxedChild(
            executable, argv, environment ? environment : empty, fds, count, sandbox, &pid);
        if (spawnError) {
            errno = spawnError;
            goto fail;
        }
    }
    closeIfOpen(readyChild);
    closeIfOpen(leaseChild);
    memset(child, 0, sizeof(*child));
    child->pid = pid;
    child->ready_fd = readyParent;
    child->lease_fd = lease;
    child->started_ms = nowMs;
    return 0;
fail:
    closeIfOpen(active);
    closeIfOpen(lease);
    closeIfOpen(leaseChild);
    closeIfOpen(pipefds[0]);
    closeIfOpen(pipefds[1]);
    closeIfOpen(readyParent);
    closeIfOpen(readyChild);
    return -1;
}

int ProcessGroup::poll(nbt_process *child, uint64_t nowMs) {
    char bytes[64];
    if (!child || child->pid <= 0 || nowMs < child->started_ms) {
        return -1;
    }
    const int running = ended(child, 1);
    if (running <= 0) {
        return running;
    }
    // A pipe or lease closed just before an exit is that exit (dev32: no restart).
    for (;;) {
        const ssize_t n = read(child->ready_fd, bytes, sizeof(bytes));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (bytes[i] != 'R') {
                child->fault = NBT_PROCESS_FAULT_PROTOCOL;
                return -1;
            }
        }
        if (n <= 0 || !hasLease(child)) {
            if (ended(child, 25) == 0) {
                return 0;
            }
            child->fault = NBT_PROCESS_FAULT_RELEASED;
            return -1;
        }
        if (!child->ready) {
            child->ready = 1;
            child->ready_ms = nowMs;
        }
        child->last_heartbeat_ms = nowMs;
    }
    if (!child->ready && nowMs - child->started_ms > 5000) {
        child->fault = NBT_PROCESS_FAULT_NOT_READY;
        return -1;
    }
    if (child->ready &&
        (nowMs < child->last_heartbeat_ms || nowMs - child->last_heartbeat_ms > 3000)) {
        child->fault = NBT_PROCESS_FAULT_HEARTBEAT;
        return -1;
    }
    return 1;
}

int ProcessGroup::healthy(const nbt_process *child, uint64_t nowMs) {
    return child && child->pid > 0 && child->ready && nowMs >= child->ready_ms &&
           nowMs - child->ready_ms >= NBT_HEALTHY_MS && nowMs >= child->last_heartbeat_ms &&
           nowMs - child->last_heartbeat_ms <= 3000 && hasLease(child);
}

int ProcessGroup::signal(const nbt_process *child, int sig) {
    if (!child || child->pid <= 0) {
        errno = ESRCH;
        return -1;
    }
    return signalTarget(child->pid, sig);
}

} // namespace process
} // namespace nbt
