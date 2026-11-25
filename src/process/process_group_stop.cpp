// SPDX-License-Identifier: MPL-2.0
// ProcessGroup's stops: at once (stop), or asked (terminate) and then reaped.
#include "nbt/process/process_group.hpp"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

namespace nbt {
namespace process {

int ProcessGroup::stop(nbt_process *child) {
    int status = 0;
    if (!child || child->pid <= 0) {
        return 0;
    }
    if (signal(child, SIGTERM) && errno != ESRCH) {
        return -1;
    }
    const int running = ended(child, 250);
    if (running <= 0) {
        if (running < 0 && errno != ECHILD) {
            return -1;
        }
        closeHandles(child);
        return 0;
    }
    if (signal(child, SIGKILL) && errno != ESRCH) {
        return -1;
    }
    while (waitpid(child->pid, &status, 0) < 0) {
        if (errno != EINTR) {
            if (errno != ECHILD) {
                return -1;
            }
            break;
        }
    }
    closeHandles(child);
    return 0;
}

int ProcessGroup::terminate(nbt_process *child, uint64_t nowMs) {
    if (!child) {
        return -1;
    }
    if (child->pid > 0 && !child->stopping) {
        if (signal(child, SIGTERM) && errno != ESRCH) {
            return -1;
        }
        child->stopping = 1;
        child->stop_ms = nowMs;
    }
    return 0;
}

int ProcessGroup::reap(nbt_process *child, uint64_t nowMs) {
    int status = 0;
    pid_t result = 0;
    if (!child || (child->pid > 0 && !child->stopping)) {
        return -1;
    }
    if (child->pid <= 0) {
        return 0;
    }
    do {
        result = waitpid(child->pid, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == child->pid || (result < 0 && errno == ECHILD)) {
        closeHandles(child);
        return 0;
    }
    if (result < 0) {
        return -1;
    }
    if (!child->killed &&
        (nowMs < child->stop_ms || nowMs - child->stop_ms >= NBT_PROCESS_STOP_GRACE_MS)) {
        if (signal(child, SIGKILL) && errno != ESRCH) {
            return -1;
        }
        child->killed = 1;
    }
    return 1;
}

} // namespace process
} // namespace nbt
