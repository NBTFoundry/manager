// SPDX-License-Identifier: MPL-2.0
#include "launcher_spawn.hpp"

#include "nbt/gui/hmi_hold.hpp"
#include "nbt/log.h"
#include "nbt/process/hmi_environment.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/slog.h>
#include <sys/slogcodes.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace plugin {
namespace {
const char kLauncher[] = "/var/opt/nbt-manager/manager/nbt-manager-launcher";
const char kGuiLock[] = "/var/opt/nbt-manager/manager/gui.lock";
} // namespace

void logError(const char *message, int error) {
    slogf(_SLOG_SETCODE(_SLOGC_TEST, 1), _SLOG_ERROR, "NbtManager: %s (errno %d)", message, error);
    nbt_logf("%s (errno %d)", message, error);
}

uint64_t nowMs() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
        return 0;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

bool guiRunning() {
    struct flock lock;
    int fd = open(kGuiLock, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    const bool locked = !fcntl(fd, F_GETLK, &lock) && lock.l_type != F_UNLCK;
    close(fd);
    return locked && nbt::gui::holdLivenessRead(nbt::gui::kHoldLivenessPath, nowMs());
}

bool spawnLauncher(const char *mode, bool includePreload) {
    static char *env[nbt::process::kHmiEnvironmentMax];
    static char preload[512];
    if (!nbt::process::hmiEnvironment(env, preload, sizeof(preload), includePreload)) {
        logError("launcher environment overflow", ENAMETOOLONG);
        return false;
    }
    int nullfd = open("/dev/null", O_RDWR);
    if (nullfd < 0) {
        logError("cannot open /dev/null", errno);
        return false;
    }
    int outfd = nbt_log_enabled() ? open(NBT_LOG_PATH, O_WRONLY | O_APPEND) : -1;
    int fdmap[3] = {nullfd, outfd >= 0 ? outfd : nullfd, outfd >= 0 ? outfd : nullfd};
    struct inheritance inherit;
    memset(&inherit, 0, sizeof(inherit));
    inherit.flags = SPAWN_NOZOMBIE | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK | SPAWN_SETGROUP;
    inherit.pgroup = SPAWN_NEWPGROUP;
    sigfillset(&inherit.sigdefault);
    sigemptyset(&inherit.sigmask);
    char *argv[] = {const_cast<char *>(kLauncher), const_cast<char *>(mode), NULL};
    nbt_logf("launcher spawn begin (%s)", mode);
    pid_t pid = spawn(kLauncher, 3, fdmap, &inherit, argv, env);
    int spawnError = errno;
    close(nullfd);
    if (outfd >= 0) {
        close(outfd);
    }
    if (pid < 0) {
        logError("cannot start launcher", spawnError);
        return false;
    }
    nbt_logf("launcher started, pid %d", static_cast<int>(pid));
    return true;
}

} // namespace plugin
} // namespace nbt
