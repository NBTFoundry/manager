#include "nbt/worker/phone_logs.hpp"

#include "nbt/log.h"
#include "nbt/log/app_log.hpp"
#include "nbt/manager_refresh.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

/* Position `fd` at its newest `limit` bytes. Empty is success; I/O errors
 * must not be returned to the phone as a successful empty log. */
bool newest(int fd, uint64_t limit, uint64_t &bytes) {
    struct stat st;
    bytes = 0;
    if (fstat(fd, &st)) {
        return false;
    }
    if (st.st_size < 0) {
        errno = EIO;
        return false;
    }
    const uint64_t size = static_cast<uint64_t>(st.st_size);
    const uint64_t skip = size > limit ? size - limit : 0;
    if (lseek(fd, static_cast<off_t>(skip), SEEK_SET) < 0) {
        return false;
    }
    bytes = size - skip;
    return true;
}

/* A file only this process can reach: created, then unlinked at once. */
int privateFile() {
#ifdef __QNX__
    const char *folder = "/dev/shmem"; // RAM: nothing written to flash
#else
    const char *folder = "/tmp";
#endif
    char path[96];
    snprintf(path, sizeof(path), "%s/nbt-manager-slog.%ld", folder, static_cast<long>(getpid()));
    (void)unlink(path);
    const int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
        (void)unlink(path);
        if (fcntl(fd, F_SETFD, FD_CLOEXEC)) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

/* "<app>\n" naming an installed application other than the manager. */
bool appBody(storage::ManagedRoot &root, const char *body, char (&app)[NBT_APP_NAME_MAX + 2]) {
    const size_t length = body ? strlen(body) : 0;
    if (!length || length >= sizeof(app) || body[length - 1] != '\n') {
        return false;
    }
    memcpy(app, body, length - 1);
    app[length - 1] = 0;
    return nbt_token_valid(app, 48, 0) && strcmp(app, "manager") &&
           nbt_fs_exists(root.legacy(), NBT_FS_PROFILE, app, NULL) == 1;
}

} // namespace

void LogReply::reset() {
    if (child > 0) {
        (void)kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    child = -1;
    files[0].reset();
    files[1].reset();
    sizes[0] = sizes[1] = 0;
}

int phoneAppLog(storage::ManagedRoot &root, const char *body, LogReply &reply) {
    char app[NBT_APP_NAME_MAX + 2];
    if (!appBody(root, body, app)) {
        return 400;
    }
    reply.reset();
    const nbt_fs_path_kind kinds[2] = {NBT_FS_APP_LOG_OLD, NBT_FS_APP_LOG};
    const char *names[2] = {"output.log.1", "output.log"};
    for (unsigned i = 0; i < 2; ++i) {
        const int fd = nbt_fs_open(root.legacy(), kinds[i], app, NULL, NBT_FS_READ);
        if (fd < 0) {
            if (errno != ENOENT) {
                nbt_logf("phone: %s log %s open failed (errno %d)", app, names[i], errno);
                reply.reset();
                return 503;
            }
            nbt_logf("phone: %s log %s missing", app, names[i]);
            continue;
        }
        reply.files[i].reset(fd);
        if (!newest(fd, 2 * log::kAppLogMax, reply.sizes[i])) {
            nbt_logf("phone: %s log %s read preparation failed (errno %d)", app, names[i], errno);
            reply.reset();
            return 503;
        }
        nbt_logf("phone: %s log %s: %llu bytes", app, names[i],
                 static_cast<unsigned long long>(reply.sizes[i]));
    }
    return 200;
}

int phoneManagerLog(storage::ManagedRoot &root, LogReply &reply) {
    reply.reset();
    const nbt_fs_path_kind kinds[2] = {NBT_FS_MANAGER_LOG_OLD, NBT_FS_MANAGER_LOG};
    for (unsigned i = 0; i < 2; ++i) {
        const int fd = nbt_fs_open(root.legacy(), kinds[i], "manager", NULL, NBT_FS_READ);
        if (fd < 0) {
            if (errno != ENOENT) {
                nbt_logf("phone: manager log open failed (errno %d)", errno);
                reply.reset();
                return 503;
            }
            continue;
        }
        reply.files[i].reset(fd);
        if (!newest(fd, kManagerLogMax, reply.sizes[i])) {
            nbt_logf("phone: manager log read preparation failed (errno %d)", errno);
            reply.reset();
            return 503;
        }
    }
    return 200;
}

int phoneClearAppLog(storage::ManagedRoot &root, const char *body) {
    char app[NBT_APP_NAME_MAX + 2];
    if (!appBody(root, body, app)) {
        return 400;
    }
    const int fd = nbt_fs_open(root.legacy(), NBT_FS_APP_LOG, app, NULL, NBT_FS_APPEND);
    const int truncated = fd >= 0 ? ftruncate(fd, 0) : -1;
    if (fd >= 0) {
        close(fd);
    }
    if (truncated ||
        (nbt_fs_unlink(root.legacy(), NBT_FS_APP_LOG_OLD, app, NULL) && errno != ENOENT)) {
        return 503;
    }
    return 200;
}

int phoneSystemLogStart(uint64_t now, LogReply &reply) {
    reply.reset();
    reply.files[0].reset(privateFile());
    if (!reply.files[0].valid() || nbt_system_log_start(reply.files[0].get(), &reply.child)) {
        nbt_logf("phone: system log unavailable (errno %d)", errno);
        reply.child = -1;
        reply.reset();
        return 503;
    }
    reply.startedMs = now;
    return 202;
}

int phoneSystemLogPoll(uint64_t now, LogReply &reply) {
    if (reply.child <= 0) {
        return 503;
    }
    int status = 0;
    pid_t done;
    do {
        done = waitpid(reply.child, &status, WNOHANG);
    } while (done < 0 && errno == EINTR);
    struct stat st;
    const bool tooLarge =
        !fstat(reply.files[0].get(), &st) && static_cast<uint64_t>(st.st_size) > kSystemLogFileMax;
    if (done == 0 && !tooLarge && now >= reply.startedMs &&
        now - reply.startedMs < kSystemLogWaitMs) {
        return 202;
    }
    if (done != reply.child) { // took too long, wrote too much, or vanished
        nbt_logf("phone: sloginfo stopped (%s)", done == 0 ? "too slow or too large" : "lost");
        reply.reset();
        return 503;
    }
    reply.child = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        nbt_logf("phone: sloginfo failed (status %d)", status);
        reply.reset();
        return 503;
    }
    if (!newest(reply.files[0].get(), kSystemLogMax, reply.sizes[0])) {
        nbt_logf("phone: system log read preparation failed (errno %d)", errno);
        reply.reset();
        return 503;
    }
    return 200;
}

} // namespace worker
} // namespace nbt
