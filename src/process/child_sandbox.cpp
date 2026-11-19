// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/child_spawn.hpp"
#include "nbt/process/sandbox_limits.hpp"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nbt {
namespace process {
ChildSandbox defaultAppSandbox() {
    ChildSandbox sb;
    sb.isolate = true;
    sb.lowerPriority = true;
    sb.memoryBytes = 256ull * 1024ull * 1024ull;
#if defined(NBT_TESTING) && defined(__SANITIZE_ADDRESS__) && !defined(__QNX__)
    sb.memoryBytes = 0; // ASan's shadow mapping cannot run under RLIMIT_DATA.
#endif
    sb.addressBytes = 512ull * 1024ull * 1024ull;
    sb.fileBytes = 128ull * 1024ull * 1024ull;
    sb.maxFiles = 128u;
    sb.maxThreads = 64u;
    sb.noCore = true;
    sb.dropUid = -1;
    sb.dropGid = -1;
    sb.chrootDir = NULL;
    return sb;
}

#ifndef __QNX__
/* The host build: fork, then limits, priority and descriptors in the child.
 * The car's QNX build spawns the --app-exec wrapper instead (child_spawn.cpp,
 * sandbox_limits.cpp). */
namespace {

void resetSignalsSafe() {
    sigset_t empty;
    sigemptyset(&empty);
    (void)sigprocmask(SIG_SETMASK, &empty, NULL);
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    static const int kSignals[] = {SIGTERM, SIGINT, SIGUSR1, SIGUSR2, SIGHUP, SIGPIPE, SIGCHLD};
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i) {
        (void)sigaction(kSignals[i], &dfl, NULL);
    }
}

bool remapDescriptorsSafe(const int *fds, unsigned count, int errWrite) {
    int mapped[kSpawnDescriptorsMax];
    for (unsigned i = 0; i < kSpawnDescriptorsMax; ++i) {
        mapped[i] = -1;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (fds[i] >= 0) {
            mapped[i] = fcntl(fds[i], F_DUPFD, static_cast<int>(count));
            if (mapped[i] < 0) {
                return false;
            }
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        if (mapped[i] >= 0) {
            if (dup2(mapped[i], static_cast<int>(i)) < 0 ||
                fcntl(static_cast<int>(i), F_SETFD, 0) < 0) {
                return false;
            }
        } else if (static_cast<int>(i) != errWrite) {
            (void)close(static_cast<int>(i));
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        if (mapped[i] >= static_cast<int>(count) && mapped[i] != errWrite) {
            (void)close(mapped[i]);
        }
    }
    return true;
}

void childFailAndExit(int errWrite, int code) {
    ssize_t n;
    do {
        n = write(errWrite, &code, sizeof(code));
    } while (n < 0 && errno == EINTR);
    (void)close(errWrite);
    _exit(127);
}

void runChildSafe(const char *path, char *const argv[], char *const env[], const int *fds,
                  unsigned count, const PreparedSandbox &prep, int errWrite) {
    (void)setpgid(0, 0);
    resetSignalsSafe();
    if (!remapDescriptorsSafe(fds, count, errWrite)) {
        childFailAndExit(errWrite, errno ? errno : EBADF);
    }
    (void)applyLimits(prep, NULL);
    if (prep.setNice) {
        (void)setpriority(PRIO_PROCESS, 0, prep.niceValue);
    }
    if (prep.chrootDir && (chroot(prep.chrootDir) != 0 || chdir("/") != 0)) {
        childFailAndExit(errWrite, errno ? errno : EPERM);
    }
    if (prep.dropGid >= 0 && setgid(static_cast<gid_t>(prep.dropGid)) != 0) {
        childFailAndExit(errWrite, errno ? errno : EPERM);
    }
    if (prep.dropUid >= 0 && setuid(static_cast<uid_t>(prep.dropUid)) != 0) {
        childFailAndExit(errWrite, errno ? errno : EPERM);
    }
    char *const empty[] = {NULL};
    execve(path, argv, env ? env : empty);
    childFailAndExit(errWrite, errno ? errno : EIO);
}

} // namespace

int spawnSandboxedChild(const char *path, char *const argv[], char *const env[], const int *fds,
                        unsigned count, const ChildSandbox *sandbox, pid_t *pid) {
    if (!sandbox || !sandbox->isolate) {
        return spawnChild(path, argv, env, fds, count, pid);
    }
    if (!path || !argv || !fds || !pid || !count || count > kSpawnDescriptorsMax) {
        return EINVAL;
    }
    int ends[2] = {-1, -1};
    if (pipe(ends) != 0) {
        return errno ? errno : EIO;
    }
    const int errRead = ends[0];
    const int errWrite = fcntl(ends[1], F_DUPFD, static_cast<int>(count));
    (void)close(ends[1]);
    if (errWrite < 0 || fcntl(errRead, F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(errWrite, F_SETFD, FD_CLOEXEC) != 0) {
        const int saved = errno ? errno : EIO;
        (void)close(errRead);
        if (errWrite >= 0) {
            (void)close(errWrite);
        }
        return saved;
    }
    PreparedSandbox prep;
    prepareSandbox(*sandbox, count, prep);
    const pid_t child = fork();
    if (child < 0) {
        const int saved = errno ? errno : EAGAIN;
        (void)close(errRead);
        (void)close(errWrite);
        return saved;
    }
    if (child == 0) {
        (void)close(errRead);
        runChildSafe(path, argv, env, fds, count, prep, errWrite);
    }
    (void)close(errWrite);
    int childErrno = 0;
    ssize_t got = 0;
    do {
        got = read(errRead, &childErrno, sizeof(childErrno));
    } while (got < 0 && errno == EINTR);
    (void)close(errRead);
    if (got > 0) {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        return childErrno ? childErrno : EIO;
    }
    *pid = child;
    return 0;
}

#endif

} // namespace process
} // namespace nbt
