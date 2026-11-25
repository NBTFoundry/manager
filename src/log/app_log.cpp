// SPDX-License-Identifier: MPL-2.0
#include "nbt/log/app_log.hpp"

#include "nbt/log.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace log {

AppLog::~AppLog() {
    if (fd_ >= 0) {
        close(fd_);
    }
    free(tail_);
}

void AppLog::retarget(nbt_fs *fs, const char *app) {
    if (fd_ >= 0) {
        sync();
        close(fd_);
        fd_ = -1;
    }
    fs_ = fs;
    app_ = app;
    dirty_ = false;
    failures_ = 0;
    tailHead_ = tailUsed_ = 0;
}

void AppLog::beginRun() {
    if (!tail_) {
        tail_ = static_cast<char *>(malloc(kRunTailMax)); // none: reports without output
    }
    tailHead_ = tailUsed_ = 0;
}

void AppLog::keep(const char *data, size_t size) {
    if (!tail_) {
        return;
    }
    if (size >= kRunTailMax) {
        memcpy(tail_, data + size - kRunTailMax, kRunTailMax);
        tailHead_ = 0;
        tailUsed_ = kRunTailMax;
        return;
    }
    const size_t first = size < kRunTailMax - tailHead_ ? size : kRunTailMax - tailHead_;
    memcpy(tail_ + tailHead_, data, first);
    memcpy(tail_, data + first, size - first);
    tailHead_ = (tailHead_ + size) % kRunTailMax;
    tailUsed_ = tailUsed_ + size < kRunTailMax ? tailUsed_ + size : kRunTailMax;
}

void AppLog::runTail(const char *&first, size_t &firstSize, const char *&second,
                     size_t &secondSize) const {
    second = tail_;
    secondSize = 0;
    if (tailUsed_ < kRunTailMax) { // not wrapped: it starts at 0
        first = tail_;
        firstSize = tailUsed_;
        return;
    }
    first = tail_ + tailHead_;
    firstSize = kRunTailMax - tailHead_;
    secondSize = tailHead_;
}

void AppLog::failed(Failure operation, const char *name, int error) {
    if (!(failures_ & static_cast<unsigned>(operation))) {
        failures_ |= static_cast<unsigned>(operation);
        nbt_logf("host %s: application log %s failed (errno %d)", app_, name, error);
    }
}

bool AppLog::open() {
    if (fd_ >= 0) {
        return true;
    }
    seekEnd_ = false;
    errno = 0;
    fd_ = -1;
#ifdef NBT_TESTING
    const char *forced = getenv("NBT_TEST_APP_LOG_CHECKED_FAILS");
    if (forced && forced[0]) {
        errno = ENOSYS;
    } else
#endif
        fd_ = nbt_fs_open(fs_, kind_, app_, NULL, NBT_FS_APPEND);
    if (fd_ >= 0) {
        (void)fcntl(fd_, F_SETFD, FD_CLOEXEC); // the application gets the pipe, not this
        return true;
    }
    const int checkedError = errno;
    char path[NBT_PATH_MAX];
    if (!nbt_fs_path(fs_, kind_, app_, NULL, path, sizeof(path))) {
        fd_ = fallbackOpen(path, checkedError);
    }
    if (fd_ < 0) {
        failed(Open, "open", checkedError);
    }
    return fd_ >= 0;
}

/* dev74/dev75 in the car: the checked open (nbt_fs_open) failed on every start
 * with errno 89, while the same open of manager/debug.log works in the same
 * process and QNX 6.5 runs the same calls without error in a VM; the cause is
 * not established. So the log does not depend on it: plain opens, accepted
 * only for a regular file of the manager's owner with one link and no group
 * or other write (the folders were checked by nbt_fs_path). Without O_APPEND,
 * each write seeks to the end. Which way worked, or why none did, goes to the
 * debug log once per host. */
int AppLog::fallbackOpen(const char *path, int checkedError) {
    struct stat st;
    const bool exists = !lstat(path, &st);
    if (exists && !S_ISREG(st.st_mode)) {
        if (!(failures_ & static_cast<unsigned>(Open))) {
            nbt_logf("host %s: application log %s: checked open failed (errno %d); not a regular "
                     "file (mode %o)",
                     app_, path, checkedError, static_cast<unsigned>(st.st_mode));
        }
        return -1;
    }
    static const struct {
        int flags;
        bool seekEnd;
        const char *name;
    } ways[] = {{O_WRONLY | O_APPEND, false, "append"},
                {O_WRONLY, true, "write"},
                {O_RDWR, true, "read-write"}};
    int errors[3] = {0, 0, 0};
    for (unsigned i = 0; i < 3; ++i) {
#ifdef NBT_TESTING
        const char *noAppend = getenv("NBT_TEST_APP_LOG_NO_APPEND");
        if (i == 0 && noAppend && noAppend[0]) {
            errors[i] = ENOSYS;
            continue;
        }
#endif
        const int fd = ::open(path, ways[i].flags | (exists ? 0 : O_CREAT | O_EXCL), 0600);
        if (fd >= 0 && !acceptable(fd)) {
            errors[i] = errno;
            close(fd);
            continue;
        }
        if (fd < 0) {
            errors[i] = errno;
            continue;
        }
        (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
        seekEnd_ = ways[i].seekEnd;
        nbt_logf("host %s: application log %s: checked open failed (errno %d); opened with a "
                 "plain %s open",
                 app_, path, checkedError, ways[i].name);
        return fd;
    }
    if (!(failures_ & static_cast<unsigned>(Open))) {
        nbt_logf("host %s: application log %s: checked open failed (errno %d); plain opens "
                 "failed too (append errno %d, write errno %d, read-write errno %d)",
                 app_, path, checkedError, errors[0], errors[1], errors[2]);
    }
    return -1;
}

bool AppLog::acceptable(int fd) const {
    struct stat st;
    if (fstat(fd, &st)) {
        return false;
    }
    if (!S_ISREG(st.st_mode) || st.st_uid != fs_->owner || st.st_dev != fs_->device ||
        st.st_nlink != 1 || (st.st_mode & 0022)) {
        errno = EPERM;
        return false;
    }
    return true;
}

bool AppLog::rotate() {
    sync();
    close(fd_);
    fd_ = -1;
    const bool renamed = !nbt_fs_rename(fs_, kind_, oldKind_, app_, NULL);
    if (!renamed) {
        failed(Rotate, "rotate", errno);
    }
    const bool opened = open();
    return renamed && opened;
}

void AppLog::sync() {
    uint64_t now = 0;
    if (fd_ >= 0 && dirty_) {
        if (fsync(fd_)) {
            failed(Sync, "sync", errno);
        } else {
            dirty_ = false;
        }
    }
    if (!nbt_monotonic_ms(&now)) {
        syncedMs_ = now;
    }
}

void AppLog::syncDue() {
    uint64_t now = 0;
    if (dirty_ && !nbt_monotonic_ms(&now) &&
        (now < syncedMs_ || now - syncedMs_ >= kAppLogSyncMs)) {
        sync();
    }
}

void AppLog::write(const char *data, size_t size) {
    struct stat st;
    if (size) {
        keep(data, size);
    }
    if (!size || !open()) {
        return;
    }
    if (fstat(fd_, &st)) {
        failed(Stat, "stat", errno);
        return;
    }
    if (size > kAppLogMax) { // only the end of an outsized chunk
        data += size - kAppLogMax;
        size = kAppLogMax;
    }
    if (st.st_size > 0 && static_cast<size_t>(st.st_size) + size > kAppLogMax) {
        if (!rotate()) {
            return;
        }
    }
    if (seekEnd_ && lseek(fd_, 0, SEEK_END) < 0) {
        failed(Write, "seek", errno);
        return;
    }
    while (size) {
        const ssize_t n = ::write(fd_, data, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            failed(Write, "write", n < 0 ? errno : EIO);
            return;
        }
        dirty_ = true;
        data += n;
        size -= static_cast<size_t>(n);
    }
}

void AppLog::note(const char *format, ...) {
    char line[256];
    const time_t now = time(NULL);
    struct tm local;
    size_t used = 0;
    if (localtime_r(&now, &local)) {
        used = strftime(line, sizeof(line), "--- %Y-%m-%d %H:%M:%S ", &local);
    }
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line + used, sizeof(line) - used - 1, format, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    used += static_cast<size_t>(n) < sizeof(line) - used - 1 ? static_cast<size_t>(n)
                                                             : sizeof(line) - used - 2;
    line[used++] = '\n';
    write(line, used);
    sync();
}

const char *signalName(int number) {
    switch (number) {
    case SIGSEGV:
        return "SIGSEGV, invalid memory access";
    case SIGBUS:
        return "SIGBUS, bus error";
    case SIGABRT:
        return "SIGABRT, aborted";
    case SIGILL:
        return "SIGILL, illegal instruction";
    case SIGFPE:
        return "SIGFPE, arithmetic error";
    case SIGKILL:
        return "SIGKILL";
    case SIGTERM:
        return "SIGTERM";
    case SIGPIPE:
        return "SIGPIPE, broken pipe";
    case SIGUSR1:
        return "SIGUSR1";
    default:
        return NULL;
    }
}

} // namespace log
} // namespace nbt
