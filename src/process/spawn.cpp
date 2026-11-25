// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/spawn.hpp"

#include "nbt/app_host.h"
#include "nbt/package.h"
#include "nbt/process/host_control.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#ifdef __QNX__
#include <spawn.h>
#endif
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

char selfPath[NBT_PATH_MAX];
char selfRoot[NBT_PATH_MAX];

/* `mode`: 0 --host, 1 --host-auto, 2 --service, 3 --service-auto. */
int startHost(nbt_fs *fs, const char *app, int mode) {
    char name[NBT_ID_MAX + 1];
    nbt::process::spawn::ChildPlan plan;
    pid_t intermediate = 0;
    if (!fs || !selfPath[0] || !nbt::process::HostControl::validApp(app)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(name, app, strlen(app) + 1);
    nbt::core::Status status = nbt::process::spawn::prepareHost(
        &plan, selfPath, name, (mode & 1) != 0, selfRoot, mode >= 2);
    if (status.ok()) {
        status = nbt::process::spawn::detach(plan, &intermediate);
    }
    if (!status.ok()) {
        errno = status.code();
        return -1;
    }
    return 0;
}

} // namespace

int nbt_app_host_set_self(const char *path, const char *test_root) {
    if (!path || path[0] != '/' || strlen(path) >= sizeof(selfPath) ||
        (test_root && strlen(test_root) >= sizeof(selfRoot))) {
        selfPath[0] = 0;
        return -1;
    }
    memcpy(selfPath, path, strlen(path) + 1);
    if (test_root) {
        memcpy(selfRoot, test_root, strlen(test_root) + 1);
    } else {
        selfRoot[0] = 0;
    }
    return 0;
}

int nbt_app_host_start(nbt_fs *fs, const char *app) {
    return startHost(fs, app, 0);
}

int nbt_app_host_autostart(nbt_fs *fs, const char *app) {
    return startHost(fs, app, 1);
}

int nbt_app_service_start(nbt_fs *fs, const char *app, int automatic) {
    const pid_t running = nbt_app_service_pid(fs, app);
    if (running) {
        return running > 0 ? 0 : -1;
    }
    return startHost(fs, app, automatic ? 3 : 2);
}

namespace nbt {
namespace process {
namespace spawn {

core::Status prepareHost(ChildPlan *plan, char *executable, const char *app, bool automatic,
                         char *testRoot, bool service) {
    if (!plan || !executable || executable[0] != '/' || !app) {
        return core::Status::error(EINVAL, "invalid child plan");
    }
    unsigned count = 0;
    plan->executable = executable;
    plan->arguments[count++] = executable;
    static const char *const modes[2][2] = {{"--host", "--host-auto"},
                                            {"--service", "--service-auto"}};
    plan->arguments[count++] = const_cast<char *>(modes[service][automatic]);
    plan->arguments[count++] = const_cast<char *>(app);
    if (testRoot && testRoot[0]) {
        plan->arguments[count++] = const_cast<char *>("--root");
        plan->arguments[count++] = testRoot;
    }
    plan->arguments[count] = NULL;
    return core::Status();
}

void execChild(const ChildPlan &plan) {
    if (setsid() < 0) {
        _exit(1);
    }
    const pid_t host = fork();
    if (host < 0) {
        _exit(1);
    }
    if (host > 0) {
        _exit(0);
    }
    const int null = open("/dev/null", O_RDWR);
    if (null < 0 || dup2(null, 0) < 0 || dup2(null, 1) < 0 || dup2(null, 2) < 0) {
        _exit(1);
    }
    if (null > 2) {
        close(null);
    }
    execv(plan.executable, plan.arguments);
    _exit(127);
}

#ifdef __QNX__
/* The host as its own session leader on /dev/null, reaped by the system
 * (spawn(): SPAWN_SETSID, SPAWN_NOZOMBIE). QNX 6.5's fork() fails with ENOSYS
 * (89) once the process has a second thread: its libc.so.3 checks
 * _Multi_threaded, and a QNX 6.5 VM returns -1/89. The worker has threads, so
 * the double fork started no host in dev70; spawn() does (dev71). */
core::Status detach(const ChildPlan &plan, pid_t *intermediate) {
    if (!intermediate) {
        return core::Status::error(EINVAL, "invalid child result");
    }
    const int null = open("/dev/null", O_RDWR);
    if (null < 0) {
        return core::Status::fromErrno("cannot open /dev/null for the application host");
    }
    const int fdmap[3] = {null, null, null};
    struct inheritance inherit;
    memset(&inherit, 0, sizeof(inherit));
    inherit.flags = SPAWN_SETSID | SPAWN_NOZOMBIE | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
    sigfillset(&inherit.sigdefault);
    sigemptyset(&inherit.sigmask);
    char *const empty[] = {NULL};
    const pid_t host =
        ::spawn(plan.executable, 3, fdmap, &inherit, plan.arguments, environ ? environ : empty);
    const int error = errno;
    close(null);
    if (host < 0) {
        errno = error;
        return core::Status::fromErrno("cannot spawn application host");
    }
    *intermediate = host;
    return core::Status();
}
#else
core::Status detach(const ChildPlan &plan, pid_t *intermediate) {
    if (!intermediate) {
        return core::Status::error(EINVAL, "invalid child result");
    }
    const pid_t child = fork();
    if (child < 0) {
        return core::Status::fromErrno("cannot fork application host");
    }
    if (!child) {
        execChild(plan);
    }
    *intermediate = child;
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return core::Status::fromErrno("cannot reap application host launcher");
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        return core::Status::error(ECHILD, "application host launcher failed");
    }
    return core::Status();
}

#endif

} // namespace spawn
} // namespace process
} // namespace nbt
