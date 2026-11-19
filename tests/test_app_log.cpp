#include "nbt/log.h"
#include "nbt/log/app_log.hpp"
#include "nbt/worker/phone_logs.hpp"
#include "support/check.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
enum Fault { None, Write, Stat, Sync, Seek };
Fault fault;
unsigned statCountdown;
ino_t logInode;
char root[256], current[320], old[320], debug[320];

bool create(const char *path, const char *text) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    TEST_CHECK(fd >= 0);
    const size_t size = strlen(text);
    TEST_CHECK(write(fd, text, size) == static_cast<ssize_t>(size));
    TEST_CHECK(close(fd) == 0);
    return true;
}

bool containsOnce(const char *text) {
    char data[4096];
    const int fd = open(debug, O_RDONLY);
    TEST_CHECK(fd >= 0);
    const ssize_t size = read(fd, data, sizeof(data) - 1);
    TEST_CHECK(size >= 0 && close(fd) == 0);
    data[size] = 0;
    const char *found = strstr(data, text);
    TEST_CHECK(found && !strstr(found + strlen(text), text));
    return true;
}

bool writerFailures(nbt_fs *fs) {
    const Fault faults[] = {Write, Stat, Sync};
    const char *messages[] = {"application log write failed", "application log stat failed",
                              "application log sync failed"};
    for (unsigned i = 0; i < 3; ++i) {
        nbt::log::AppLog log(fs, "test");
        log.note("initial"); // Open the log before injecting an error.
        struct stat st;
        TEST_CHECK(stat(current, &st) == 0);
        logInode = st.st_ino;
        TEST_CHECK(create(debug, ""));
        fault = faults[i];
        log.note("failure probe");
        log.note("failure probe again");
        fault = None;
        TEST_CHECK(containsOnce(messages[i]));
        struct stat after;
        TEST_CHECK(stat(current, &after) == 0);
        TEST_CHECK(faults[i] == Sync || after.st_size == st.st_size);
        log.note("recovered");
        TEST_CHECK(stat(current, &st) == 0 && st.st_size > after.st_size);
    }
    return true;
}

bool rotationFailure(nbt_fs *fs) {
    TEST_CHECK(create(current, "") && mkdir(old, 0700) == 0);
    nbt::log::AppLog log(fs, "test");
    static char full[nbt::log::kAppLogMax];
    memset(full, 'x', sizeof(full));
    log.write(full, sizeof(full));
    TEST_CHECK(create(debug, ""));
    log.write("next", 4);
    log.write("next", 4);
    struct stat st;
    TEST_CHECK(stat(current, &st) == 0 && st.st_size == sizeof(full));
    TEST_CHECK(containsOnce("application log rotate failed"));
    TEST_CHECK(rmdir(old) == 0);
    log.write("after rotation", 14);
    TEST_CHECK(stat(current, &st) == 0 && st.st_size == 14);
    return true;
}

bool readerFailure(nbt::storage::ManagedRoot &managed) {
    nbt::worker::LogReply reply;
    TEST_CHECK(unlink(current) == 0 && unlink(old) == 0);
    TEST_CHECK(create(debug, ""));
    TEST_CHECK(nbt::worker::phoneAppLog(managed, "test\n", reply) == 200 && reply.total() == 0);
    TEST_CHECK(containsOnce("test log output.log missing"));
    TEST_CHECK(create(current, "existing log\n"));
    struct stat st;
    TEST_CHECK(stat(current, &st) == 0);
    logInode = st.st_ino;
    const Fault faults[] = {Stat, Seek};
    for (unsigned i = 0; i < 2; ++i) {
        TEST_CHECK(create(debug, ""));
        // Let the path policy check the opened file; fail the reader's own fstat.
        statCountdown = 2;
        fault = faults[i];
        const int status = nbt::worker::phoneAppLog(managed, "test\n", reply);
        fault = None;
        statCountdown = 0;
        TEST_CHECK(status == 503 && reply.total() == 0 && !reply.files[1].valid());
        TEST_CHECK(containsOnce("test log output.log read preparation failed"));
    }
    TEST_CHECK(nbt::worker::phoneAppLog(managed, "test\n", reply) == 200 && reply.total() == 13);
    TEST_CHECK(create(current, ""));
    TEST_CHECK(nbt::worker::phoneAppLog(managed, "test\n", reply) == 200 && reply.total() == 0);
    return true;
}
} // namespace

extern "C" {
int __real_fstat(int fd, struct stat *st);
ssize_t __real_write(int fd, const void *data, size_t size);
int __real_fsync(int fd);
off_t __real_lseek(int fd, off_t offset, int whence);

static bool isLog(int fd) {
    struct stat st;
    return !__real_fstat(fd, &st) && logInode && st.st_ino == logInode;
}

int __wrap_fstat(int fd, struct stat *st) {
    if (fault == Stat && isLog(fd) && (!statCountdown || !--statCountdown)) {
        errno = EIO;
        return -1;
    }
    return __real_fstat(fd, st);
}

ssize_t __wrap_write(int fd, const void *data, size_t size) {
    if (fault == Write && isLog(fd)) {
        errno = ENOSPC;
        return -1;
    }
    return __real_write(fd, data, size);
}

int __wrap_fsync(int fd) {
    if (fault == Sync && isLog(fd)) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

off_t __wrap_lseek(int fd, off_t offset, int whence) {
    if (fault == Seek && isLog(fd)) {
        errno = EIO;
        return -1;
    }
    return __real_lseek(fd, offset, whence);
}
}

static bool run() {
    snprintf(root, sizeof(root), "/tmp/nbt-app-log-XXXXXX");
    TEST_CHECK(mkdtemp(root));
    const char *folders[] = {"manager", "state", "state/test", "trust", "trust/apps"};
    char path[320];
    for (unsigned i = 0; i < 5; ++i) {
        snprintf(path, sizeof(path), "%s/%s", root, folders[i]);
        TEST_CHECK(mkdir(path, 0700) == 0);
    }
    snprintf(current, sizeof(current), "%s/state/test/output.log", root);
    snprintf(old, sizeof(old), "%s/state/test/output.log.1", root);
    snprintf(debug, sizeof(debug), "%s/manager/debug.log", root);
    snprintf(path, sizeof(path), "%s/trust/apps/test.conf", root);
    TEST_CHECK(create(path, "NBTAPP1\ncompat=demo-1\n") && create(debug, ""));
    nbt::storage::ManagedRoot managed;
    TEST_CHECK(managed.initialize(root).ok());
    TEST_CHECK(nbt_log_open(managed.legacy(), "test") == 1);
    TEST_CHECK(writerFailures(managed.legacy()));
    TEST_CHECK(rotationFailure(managed.legacy()));
    TEST_CHECK(readerFailure(managed));
    nbt_log_close();
    TEST_CHECK(unlink(current) == 0 && unlink(debug) == 0 && unlink(path) == 0);
    for (unsigned i = 5; i > 0; --i) {
        snprintf(path, sizeof(path), "%s/%s", root, folders[i - 1]);
        TEST_CHECK(rmdir(path) == 0);
    }
    TEST_CHECK(rmdir(root) == 0);
    return true;
}

int main() {
    return run() ? 0 : 1;
}
