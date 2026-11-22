// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/app_output.hpp"

#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

namespace nbt {
namespace process {

AppOutput::~AppOutput() {
    if (fd_ >= 0) {
        close(fd_);
    }
    if (reportFd_ >= 0) {
        close(reportFd_);
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

bool AppOutput::ended(const nbt_process &child) {
    drain();
    const log::CrashEnding how = {child.exit_status, child.exit_signal, child.fault};
    const bool crash = log::describeEnding(how, ending_, sizeof(ending_));
    log_.note("%s", ending_);
    return crash;
}

void AppOutput::beginRun(const char *version, uint64_t sequence, long pid, const char *start) {
    snprintf(version_, sizeof(version_), "%s", version ? version : "");
    sequence_ = sequence;
    pid_ = pid; // the process record forgets it once reaped
    start_ = start;
    if (nbt_monotonic_ms(&startedMs_)) {
        startedMs_ = UINT64_MAX;
    }
    log_.beginRun();
}

int AppOutput::report(nbt_fs *fs, const char *app, bool systemLog) {
    uint64_t now = 0;
    const bool timed = startedMs_ != UINT64_MAX && !nbt_monotonic_ms(&now) && now >= startedMs_;
    const log::CrashFacts facts = {
        app, version_, sequence_, start_, pid_, timed ? now - startedMs_ : UINT64_MAX, ending_};
    log::CrashSection output = {"Its output and its host's notes, the end of this run", {}, {}};
    log_.runTail(output.data[0], output.size[0], output.data[1], output.size[1]);
    if (reportFd_ >= 0) {
        close(reportFd_);
        reportFd_ = -1;
    }
    return log::writeCrashReport(fs, facts, &output, 1, systemLog ? &reportFd_ : NULL);
}

void AppOutput::finishReport() {
    if (reportFd_ >= 0) {
        log::appendCrashSystemLog(reportFd_);
        close(reportFd_);
        reportFd_ = -1;
    }
}

} // namespace process
} // namespace nbt
