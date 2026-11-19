// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/sandbox_limits.hpp"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

namespace nbt {
namespace process {
namespace {

char wrapperPath[NBT_PATH_MAX];

void addLimit(PreparedSandbox &prep, int resource, const char *name, bool bytes, uint64_t cap,
              uint64_t floor) {
    struct rlimit cur;
    if (prep.limitCount >= sizeof(prep.limits) / sizeof(prep.limits[0]) ||
        getrlimit(resource, &cur) != 0) {
        return;
    }
    const uint64_t desired = cap < floor ? floor : cap;
    const rlim_t maxVal = cur.rlim_max;
    rlim_t target = static_cast<rlim_t>(desired);
    if (static_cast<uint64_t>(target) != desired) {
        target = maxVal;
    }
#ifdef RLIM_SAVED_MAX
    const bool finite =
        maxVal != RLIM_INFINITY && maxVal != RLIM_SAVED_MAX && maxVal != RLIM_SAVED_CUR;
#else
    const bool finite = maxVal != RLIM_INFINITY;
#endif
    if (finite && target > maxVal) {
        target = maxVal;
    }
    PreparedLimit &entry = prep.limits[prep.limitCount++];
    entry.resource = resource;
    entry.name = name;
    entry.bytes = bytes;
    entry.value.rlim_cur = target;
    entry.value.rlim_max = target;
}

void writeAll(int fd, const char *text, size_t size) {
    while (size) {
        const ssize_t n = write(fd, text, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return;
        }
        text += n;
        size -= static_cast<size_t>(n);
    }
}

void report(const char *text) {
    writeAll(2, text, strlen(text));
}

} // namespace

void prepareSandbox(const ChildSandbox &sb, unsigned fdCount, PreparedSandbox &prep) {
    memset(&prep, 0, sizeof(prep));
    prep.dropUid = sb.dropUid;
    prep.dropGid = sb.dropGid;
    prep.chrootDir = sb.chrootDir;
    if (sb.noCore) {
        addLimit(prep, RLIMIT_CORE, "core", true, 0, 0);
    }
    if (sb.maxFiles) {
        addLimit(prep, RLIMIT_NOFILE, "files", false, sb.maxFiles, fdCount > 16 ? fdCount : 16u);
    }
#ifndef __QNX__
    // QNX 6.5 refuses RLIMIT_FSIZE with EINVAL, whatever the value (QNX 6.5 VM).
    if (sb.fileBytes) {
        addLimit(prep, RLIMIT_FSIZE, "file size", true, sb.fileBytes, 1024u * 1024u);
    }
#endif
    if (sb.memoryBytes) {
        addLimit(prep, RLIMIT_DATA, "data", true, sb.memoryBytes, 16u * 1024u * 1024u);
    }
#ifdef RLIMIT_NTHR
    if (sb.maxThreads) {
        addLimit(prep, RLIMIT_NTHR, "threads", false, sb.maxThreads, 4u);
    }
#endif
    if (sb.lowerPriority) {
        errno = 0;
        const int cur = getpriority(PRIO_PROCESS, 0);
        if (!errno) {
            prep.setNice = true;
            prep.niceValue = cur + 5 > 19 ? 19 : cur + 5;
        }
    }
}

unsigned applyLimits(const PreparedSandbox &prep, int *error) {
    unsigned failed = 0;
    for (size_t i = 0; i < prep.limitCount; ++i) {
        if (setrlimit(prep.limits[i].resource, &prep.limits[i].value) != 0) {
            ++failed;
            if (error) {
                *error = errno;
            }
        }
    }
    return failed;
}

void setAppExecWrapper(const char *self) {
    if (self && self[0] == '/' && strlen(self) < sizeof(wrapperPath)) {
        memcpy(wrapperPath, self, strlen(self) + 1);
    } else {
        wrapperPath[0] = 0;
    }
}

const char *appExecWrapper() {
    return wrapperPath[0] ? wrapperPath : NULL;
}

int appExecMain(int argc, char **argv) {
    char line[256];
    if (argc < 4 || !argv[2] || argv[2][0] != '/') {
        report("nbt-managerd: --app-exec PATH ARGV0 [ARGS...]\n");
        return 127;
    }
    PreparedSandbox prep;
    prepareSandbox(defaultAppSandbox(), kSpawnDescriptorsMax, prep);
    size_t used = static_cast<size_t>(snprintf(line, sizeof(line), "nbt-managerd: limits:"));
    for (size_t i = 0; i < prep.limitCount && used < sizeof(line); ++i) {
        const unsigned long value = static_cast<unsigned long>(prep.limits[i].value.rlim_cur);
        const int n = prep.limits[i].bytes && value >= 1024ul * 1024ul
                          ? snprintf(line + used, sizeof(line) - used, "%s %s %lu MiB",
                                     i ? "," : "", prep.limits[i].name, value >> 20)
                          : snprintf(line + used, sizeof(line) - used, "%s %s %lu", i ? "," : "",
                                     prep.limits[i].name, value);
        used += n > 0 ? static_cast<size_t>(n) : 0;
    }
    int error = 0;
    const unsigned failed = applyLimits(prep, &error);
    if (failed && used < sizeof(line)) {
        snprintf(line + used, sizeof(line) - used, "; %u not set (errno %d)", failed, error);
    }
    used = strlen(line);
    if (used + 1 < sizeof(line)) {
        line[used++] = '\n';
        line[used] = 0;
    } else {
        line[used - 1] = '\n';
    }
    report(line);
    execve(argv[2], argv + 3, environ);
    snprintf(line, sizeof(line), "nbt-managerd: cannot start %.160s (errno %d)\n", argv[2], errno);
    report(line);
    return 127;
}

} // namespace process
} // namespace nbt
