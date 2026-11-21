// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/app_output.hpp"

#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace nbt {
namespace process {

AppOutput::~AppOutput() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

int AppOutput::open() {
    int ends[2];
    if (fd_ >= 0 || pipe(ends)) {
        return -1;
    }
    const int flags = fcntl(ends[0], F_GETFL);
    if (flags < 0 || fcntl(ends[0], F_SETFL, flags | O_NONBLOCK) ||
        fcntl(ends[0], F_SETFD, FD_CLOEXEC) || fcntl(ends[1], F_SETFD, FD_CLOEXEC)) {
        close(ends[0]);
        close(ends[1]);
        return -1;
    }
    fd_ = ends[0];
    return ends[1];
}

bool AppOutput::start() {
    const int write = open();
    if (write < 0) {
        return false;
    }
    // dup2 clears close-on-exec on 1 and 2, which the application inherits.
    const bool redirected = dup2(write, STDOUT_FILENO) >= 0 && dup2(write, STDERR_FILENO) >= 0;
    if (write > STDERR_FILENO) {
        close(write);
    }
    if (!redirected) {
        close(fd_);
        fd_ = -1;
    }
    return redirected;
}

void AppOutput::retarget(nbt_fs *fs, const char *app) {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    log_.retarget(fs, app);
}

void AppOutput::drain() {
    char buffer[4096];
    while (fd_ >= 0) {
        const ssize_t n = read(fd_, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        log_.write(buffer, static_cast<size_t>(n));
    }
    log_.syncDue();
}

void AppOutput::stopChild(nbt_process &child) {
    uint64_t now = 0;
    if (nbt_monotonic_ms(&now) || nbt_process_terminate(&child, now)) {
        (void)nbt_process_stop(&child);
        return;
    }
    for (;;) {
        drain();
        const int reaped = nbt_monotonic_ms(&now) ? -1 : nbt_process_reap(&child, now);
        if (reaped <= 0) {
            if (reaped < 0) {
                (void)nbt_process_stop(&child);
            }
            return;
        }
        struct pollfd readable = {fd_, POLLIN, 0};
        (void)poll(&readable, 1, 20);
    }
}

void AppOutput::ended(const nbt_process &child) {
    drain();
    switch (child.fault) {
    case NBT_PROCESS_FAULT_NOT_READY:
        log_.note("stopped: it did not signal ready (write 'R' to fd 3) within 5 s");
        return;
    case NBT_PROCESS_FAULT_HEARTBEAT:
        log_.note("stopped: no heartbeat ('R' on fd 3) for 3 s");
        return;
    case NBT_PROCESS_FAULT_PROTOCOL:
        log_.note("stopped: it wrote something other than 'R' to fd 3");
        return;
    case NBT_PROCESS_FAULT_RELEASED:
        log_.note("stopped: it closed fd 3 or released its lease on fd 4 but kept running");
        return;
    default:
        break;
    }
    if (child.exit_signal) {
        const char *name = log::signalName(child.exit_signal);
        log_.note("ended by signal %d%s%s%s", child.exit_signal, name ? " (" : "", name ? name : "",
                  name ? ")" : "");
    } else {
        log_.note("exited with status %d", child.exit_status);
    }
}

} // namespace process
} // namespace nbt
