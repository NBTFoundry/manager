// SPDX-License-Identifier: MPL-2.0
/* The system log section of a crash report (nbt/log/crash_report.hpp). */
#include "nbt/log/crash_report.hpp"

#include "nbt/log.h"
#include "nbt/manager_refresh.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nbt {
namespace log {
namespace {

/* Keeps the newest `kCrashSystemLines` complete lines that tell of a fault:
 * the dumper's "run fault pid <pid> ... ip <address> <program>" and the
 * process manager's "terminated SIG...". */
struct FaultLines {
    FaultLines() : count(0), next(0), used(0) {
    }
    void feed(const char *data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            if (data[i] == '\n') {
                line[used] = 0;
                take();
                used = 0;
            } else if (used + 1 < sizeof(line)) {
                line[used++] = data[i];
            }
        }
    }
    void take() {
        if (!strstr(line, "run fault pid") && !strstr(line, "terminated SIG")) {
            return;
        }
        memcpy(kept[next], line, used + 1); // used < sizeof(line) == sizeof(kept[next])
        next = (next + 1) % kCrashSystemLines;
        if (count < kCrashSystemLines) {
            ++count;
        }
    }
    char line[256];
    char kept[kCrashSystemLines][256];
    unsigned count, next;
    size_t used;
};

/* Runs sloginfo into `faults` for at most kCrashSystemWaitMs: NULL, or what went wrong. */
const char *readSystemLog(FaultLines &faults) {
    int ends[2];
    if (pipe(ends)) {
        return "unavailable";
    }
    (void)fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    pid_t child = -1;
    const char *problem = NULL;
    if (nbt_system_log_start(ends[1], &child)) {
        problem = "unavailable";
        child = -1;
    }
    close(ends[1]);
    uint64_t started = 0, now = 0;
    (void)nbt_monotonic_ms(&started);
    while (child > 0) {
        if (nbt_monotonic_ms(&now) || now - started >= kCrashSystemWaitMs) {
            problem = "sloginfo took too long";
            break;
        }
        struct pollfd readable = {ends[0], POLLIN, 0};
        const int ready = poll(&readable, 1, 200);
        if (ready < 0 && errno != EINTR) {
            problem = "unavailable";
            break;
        }
        if (ready <= 0) {
            continue;
        }
        char buffer[4096];
        const ssize_t n = read(ends[0], buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break; // the end of its output
        }
        faults.feed(buffer, static_cast<size_t>(n));
    }
    close(ends[0]);
    if (child > 0) {
        (void)kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    return problem;
}

bool writeLine(int fd, const char *text) {
    const size_t size = strlen(text);
    size_t done = 0;
    while (done < size) {
        const ssize_t n = write(fd, text + done, size - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return write(fd, "\n", 1) == 1;
}

} // namespace

void appendCrashSystemLog(int fd) {
    if (fd < 0) {
        return;
    }
    FaultLines faults;
    const char *problem = readSystemLog(faults);
    bool written = lseek(fd, 0, SEEK_END) >= 0 &&
                   writeLine(fd, "The system log's newest fault lines (sloginfo, any process):");
    if (problem || !faults.count) {
        char note[64];
        snprintf(note, sizeof(note), "(%s)", problem ? problem : "none");
        written = written && writeLine(fd, note);
    }
    for (unsigned i = 0; written && i < faults.count; ++i) {
        written = writeLine(
            fd,
            faults.kept[(faults.next + kCrashSystemLines - faults.count + i) % kCrashSystemLines]);
    }
    if (!written || fsync(fd)) {
        nbt_logf("crash report: system log lines not added (errno %d)", errno);
    }
}

} // namespace log
} // namespace nbt
