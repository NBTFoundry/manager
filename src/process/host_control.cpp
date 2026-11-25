// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/host_control.hpp"

#include "nbt/app_host.h"
#include "nbt/log.h"
#include "nbt/most_service.h"
#include "nbt/package.h"
#include "nbt/storage/byte_lock.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

/* Byte 0 is the lifetime identity lock. A ready host additionally holds a
 * read lock on byte 1 for show and byte 3 for status, only after readiness. A whole-file
 * writer (including an older host) cannot be mistaken for that marker.
 * Kernel locks disappear on exit; there is no stale PID record to parse. */
pid_t readyOwner(nbt_fs *fs, const char *app, off_t byte) {
    struct flock owner, marker, again;
    pid_t pid = 0;
    int result = -1;
    if (!fs || !nbt::process::HostControl::validApp(app)) {
        errno = EINVAL;
        return -1;
    }
    const int fd = nbt_fs_open(fs, NBT_FS_HOST_LOCK, app, NULL, NBT_FS_READ);
    if (fd < 0) {
        return -1;
    }
    memset(&owner, 0, sizeof(owner));
    owner.l_type = F_WRLCK;
    owner.l_whence = SEEK_SET;
    owner.l_len = 1;
    if (fcntl(fd, F_GETLK, &owner)) {
        goto done;
    }
    if (owner.l_type != F_WRLCK || owner.l_pid <= 0) {
        errno = ESRCH;
        goto done;
    }
    pid = owner.l_pid;
    memset(&marker, 0, sizeof(marker));
    marker.l_type = F_WRLCK;
    marker.l_whence = SEEK_SET;
    marker.l_start = byte;
    marker.l_len = 1;
    if (fcntl(fd, F_GETLK, &marker)) {
        goto done;
    }
    if (marker.l_type != F_RDLCK || marker.l_pid != pid || marker.l_start != byte ||
        marker.l_len != 1) {
        result = 0;
        goto done;
    }
    memset(&again, 0, sizeof(again));
    again.l_type = F_WRLCK;
    again.l_whence = SEEK_SET;
    again.l_len = 1;
    if (fcntl(fd, F_GETLK, &again)) {
        goto done;
    }
    if (again.l_type != F_WRLCK || again.l_pid != pid) {
        errno = ESRCH;
        goto done;
    }
    result = pid;
done:
    const int saved = errno;
    if (close(fd) && result >= 0) {
        return -1;
    }
    errno = saved;
    return result;
}

} // namespace

static pid_t lockOwner(nbt_fs *fs, const char *app, nbt_fs_path_kind kind) {
    struct flock lock;
    if (!fs || !nbt::process::HostControl::validApp(app)) {
        errno = EINVAL;
        return -1;
    }
    const int fd = nbt_fs_open(fs, kind, app, NULL, NBT_FS_READ);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_len = 1;
    if (fcntl(fd, F_GETLK, &lock)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    close(fd);
    if (lock.l_type == F_UNLCK) {
        return 0;
    }
    return (lock.l_type == F_WRLCK && lock.l_pid > 0) ? lock.l_pid : -1;
}

static int signalOwner(pid_t pid, int signal_number) {
    if (pid <= 0) {
        return pid;
    }
    return kill(pid, signal_number) && errno != ESRCH ? -1 : 0;
}

pid_t nbt_app_host_pid(nbt_fs *fs, const char *app) {
    return lockOwner(fs, app, NBT_FS_HOST_LOCK);
}

int nbt_app_host_signal(nbt_fs *fs, const char *app, int signal_number) {
    return signalOwner(nbt_app_host_pid(fs, app), signal_number);
}

pid_t nbt_app_service_pid(nbt_fs *fs, const char *app) {
    return lockOwner(fs, app, NBT_FS_SERVICE_LOCK);
}

int nbt_app_service_signal(nbt_fs *fs, const char *app, int signal_number) {
    return signalOwner(nbt_app_service_pid(fs, app), signal_number);
}

int nbt_app_host_show(nbt_fs *fs, const char *app) {
    const pid_t pid = readyOwner(fs, app, 1);
    if (pid < 0) {
        return -1;
    }
    if (!pid) {
        return 1;
    }
    if (kill(pid, SIGUSR1)) {
        return -1;
    }
    nbt_logf("host %s: show requested", app);
    return 0;
}

int nbt_app_host_status(nbt_fs *fs, const char *app, char text[NBT_BRIDGE_STATUS_MAX + 1]) {
    char line[NBT_BRIDGE_STATUS_LINE_MAX + 1];
    uint64_t sequence = 0;
    ssize_t n = 0;
    int result = -1;
    if (!text) {
        return -1;
    }
    text[0] = 0;
    const pid_t owner = readyOwner(fs, app, 3);
    if (owner <= 0) {
        return 1;
    }
    const int fd = nbt_fs_status_open(fs, app, 0);
    if (fd < 0) {
        return 1;
    }
    do {
        n = read(fd, line, sizeof(line));
    } while (n < 0 && errno == EINTR);
    if (n > 0 && !nbt_bridge_status_parse(line, static_cast<size_t>(n), &sequence, text) &&
        readyOwner(fs, app, 3) == owner) {
        result = 0;
    }
    if (close(fd)) {
        result = -1;
    }
    if (result) {
        text[0] = 0;
    }
    return result;
}

namespace nbt {
namespace process {

bool HostControl::validApp(const char *app) {
    return nbt_token_valid(app, NBT_ID_MAX, 0) && strcmp(app, "manager") != 0;
}

int HostControl::setByteLock(int fd, short type, off_t byte) {
    return storage::ByteLock::set(fd, type, byte) ? 0 : -1;
}

int HostControl::prepareMostChannel(const char *app, const nbt_bridge_requirement &bridge,
                                    const char *served, char available[NBT_BRIDGE_CAPS_MAX + 1],
                                    int *controller) {
    nbt_most_policy rules;
    memset(&rules, 0, sizeof(rules)); // `controller` alone: no raw functions
    const char *library = NULL;
    int host = -1, child = -1, feed = -1, feedChild = -1;
#ifdef NBT_TESTING
    library = getenv("NBT_TEST_DSI_LIBRARY");
#endif
    if (bridge.most[0] && nbt_bridge_cap_has(served, "most") &&
        (nbt_most_policy_parse(bridge.most, &rules) || nbt_most_service_socket(&host, &child))) {
        nbt_logf("host %s: MOST unavailable (policy or socket)", app);
        memset(&rules, 0, sizeof(rules));
    }
    if ((nbt_bridge_cap_has(bridge.uses, "controller") ||
         nbt_bridge_cap_has(bridge.optional, "controller")) &&
        nbt_bridge_cap_has(served, "controller") && nbt_most_service_socket(&feed, &feedChild)) {
        nbt_logf("host %s: controller unavailable (socket)", app);
    }
    if ((host >= 0 || feed >= 0) && nbt_most_service_start(library, app, &rules, host, feed)) {
        nbt_logf("host %s: MOST and controller unavailable (no DSI client)", app);
        const int fds[] = {host, child, feed, feedChild};
        for (unsigned i = 0; i < 4; ++i) {
            if (fds[i] >= 0) {
                close(fds[i]);
            }
        }
        child = feedChild = -1;
    }
    char withoutMost[NBT_BRIDGE_CAPS_MAX + 1];
    nbt_bridge_caps_without(served, child >= 0 ? "" : "most", withoutMost);
    nbt_bridge_caps_without(withoutMost, feedChild >= 0 ? "" : "controller", available);
    *controller = feedChild;
    return child;
}

core::Status RealHostControl::start(const storage::ManagedRoot &root, const char *app,
                                    bool automatic) {
    if (!root.valid()) {
        return core::Status::error(EINVAL, "managed root is not initialized");
    }
    const int result = automatic ? nbt_app_host_autostart(const_cast<nbt_fs *>(root.legacy()), app)
                                 : nbt_app_host_start(const_cast<nbt_fs *>(root.legacy()), app);
    if (result) {
        return core::Status::error(errno ? errno : EIO, "application host start failed");
    }
    return core::Status();
}

core::Result<pid_t> RealHostControl::servicePid(const storage::ManagedRoot &root, const char *app) {
    const pid_t result =
        root.valid() ? nbt_app_service_pid(const_cast<nbt_fs *>(root.legacy()), app) : -1;
    if (result < 0) {
        return core::Result<pid_t>::failure(core::Status::fromErrno("service lookup failed"));
    }
    return core::Result<pid_t>::success(result);
}

core::Status RealHostControl::serviceSignal(const storage::ManagedRoot &root, const char *app,
                                            int signalNumber) {
    if (!root.valid() ||
        nbt_app_service_signal(const_cast<nbt_fs *>(root.legacy()), app, signalNumber)) {
        return core::Status::error(errno ? errno : EIO, "service signal failed");
    }
    return core::Status();
}

core::Status RealHostControl::serviceStart(const storage::ManagedRoot &root, const char *app) {
    if (!root.valid() || nbt_app_service_start(const_cast<nbt_fs *>(root.legacy()), app, 0)) {
        return core::Status::error(errno ? errno : EIO, "service start failed");
    }
    return core::Status();
}

core::Result<pid_t> RealHostControl::pid(const storage::ManagedRoot &root, const char *app) {
    if (!root.valid()) {
        return core::Result<pid_t>::failure(core::Status::error(EINVAL, "invalid root"));
    }
    const pid_t result = nbt_app_host_pid(const_cast<nbt_fs *>(root.legacy()), app);
    if (result < 0) {
        return core::Result<pid_t>::failure(core::Status::fromErrno("host lookup failed"));
    }
    return core::Result<pid_t>::success(result);
}

core::Status RealHostControl::signal(const storage::ManagedRoot &root, const char *app,
                                     int signalNumber) {
    if (!root.valid() ||
        nbt_app_host_signal(const_cast<nbt_fs *>(root.legacy()), app, signalNumber)) {
        return core::Status::error(errno ? errno : EIO, "application host signal failed");
    }
    return core::Status();
}

core::Result<bool> RealHostControl::show(const storage::ManagedRoot &root, const char *app) {
    if (!root.valid()) {
        return core::Result<bool>::failure(core::Status::error(EINVAL, "invalid root"));
    }
    const int result = nbt_app_host_show(const_cast<nbt_fs *>(root.legacy()), app);
    if (result < 0) {
        return core::Result<bool>::failure(core::Status::fromErrno("host show failed"));
    }
    return core::Result<bool>::success(result != 0);
}

core::Status RealHostControl::status(const storage::ManagedRoot &root, const char *app,
                                     core::Span<char> text) {
    if (text.size() != NBT_BRIDGE_STATUS_MAX + 1) {
        return core::Status::error(EINVAL, "invalid host status buffer");
    }
    if (!root.valid() ||
        nbt_app_host_status(const_cast<nbt_fs *>(root.legacy()), app, text.data())) {
        return core::Status::error(errno ? errno : EIO, "application status unavailable");
    }
    return core::Status();
}

} // namespace process
} // namespace nbt
