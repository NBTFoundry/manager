// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/child_spawn.hpp"
#include "nbt/process/sandbox_limits.hpp"

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace process {

#ifdef NBT_TESTING
static void buildTestArgs(char *argv[16], unsigned &argc, const char **executable) {
    if (getenv("NBT_TEST_MANAGER_GUI") && getenv("NBT_TEST_MANAGER_ROOT") &&
        getenv("NBT_TEST_MANAGER_WORKER")) {
        *executable = getenv("NBT_TEST_MANAGER_GUI");
        argv[0] = const_cast<char *>(*executable);
        argv[argc++] = const_cast<char *>("--test-root");
        argv[argc++] = getenv("NBT_TEST_MANAGER_ROOT");
        argv[argc++] = const_cast<char *>("--test-worker");
        argv[argc++] = getenv("NBT_TEST_MANAGER_WORKER");
    }
}
#endif

void buildProcessArgv(char *argv[16], char *path, nbt_fs_path_kind kind, const char *slot,
                      const char **executable, const char *runner) {
    unsigned argc = 0;
    *executable = runner ? runner : path;
    argv[argc++] = runner ? const_cast<char *>(runner) : path;
#ifdef NBT_TESTING
    if (kind == NBT_FS_MANAGER_SLOT_GUI) {
        buildTestArgs(argv, argc, executable);
    }
#endif
    if (kind == NBT_FS_MANAGER_SLOT_GUI) {
        argv[argc++] = const_cast<char *>("--slot");
        argv[argc++] = const_cast<char *>(slot);
    }
    argv[argc++] = const_cast<char *>("--ready-fd");
    argv[argc++] = const_cast<char *>("3");
    argv[argc++] = const_cast<char *>("--lease-fd");
    argv[argc++] = const_cast<char *>("4");
    if (kind == NBT_FS_MANAGER_SLOT_GUI) {
        argv[argc++] = const_cast<char *>("--worker-fd");
        argv[argc++] = const_cast<char *>("5");
    }
    if (runner) {
        argv[argc++] = const_cast<char *>("--nbt-payload");
        argv[argc++] = path;
    }
    argv[argc] = NULL;
}

#ifdef __QNX__

int spawnChild(const char *path, char *const argv[], char *const env[], const int *fds,
               unsigned count, pid_t *pid) {
    int fdmap[kSpawnDescriptorsMax];
    if (!path || !argv || !env || !fds || !pid || !count || count > kSpawnDescriptorsMax) {
        return EINVAL;
    }
    for (unsigned i = 0; i < count; ++i) {
        fdmap[i] = fds[i] >= 0 ? fds[i] : SPAWN_FDCLOSED;
    }
    struct inheritance inherit;
    memset(&inherit, 0, sizeof(inherit));
    inherit.flags = SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
    sigfillset(&inherit.sigdefault);
    sigemptyset(&inherit.sigmask);
    const pid_t child = ::spawn(path, static_cast<int>(count), fdmap, &inherit, argv, env);
    if (child < 0) {
        return errno ? errno : EIO;
    }
    *pid = child;
    return 0;
}

/* An application in its own process group (SPAWN_SETGROUP, SPAWN_NEWPGROUP),
 * with default signals, at a lower priority (SPAWN_EXPLICIT_SCHED). The car
 * uses spawn() as the manager's other processes do: QNX 6.5's fork() fails
 * with ENOSYS in a process with threads (libc.so.3's _Multi_threaded check),
 * and the host has the MOST client's threads. struct inheritance has no
 * resource limits, so what is spawned is the --app-exec wrapper, which sets
 * them and execve()s the application (sandbox_limits.hpp). */
int spawnSandboxedChild(const char *path, char *const argv[], char *const env[], const int *fds,
                        unsigned count, const ChildSandbox *sandbox, pid_t *pid) {
    char *const empty[] = {NULL};
    if (!sandbox || !sandbox->isolate) {
        return spawnChild(path, argv, env ? env : empty, fds, count, pid);
    }
    int fdmap[kSpawnDescriptorsMax];
    if (!path || !argv || !fds || !pid || !count || count > kSpawnDescriptorsMax) {
        return EINVAL;
    }
    enum { kArgsMax = 16 };
    char *wrapped[kArgsMax + 4];
    const char *wrapper = appExecWrapper();
    if (wrapper) {
        unsigned n = 0;
        wrapped[n++] = const_cast<char *>(wrapper);
        wrapped[n++] = const_cast<char *>("--app-exec");
        wrapped[n++] = const_cast<char *>(path);
        for (unsigned i = 0; argv[i]; ++i) {
            if (i >= kArgsMax) {
                return E2BIG;
            }
            wrapped[n++] = argv[i];
        }
        wrapped[n] = NULL;
        path = wrapper;
        argv = wrapped;
    }
    for (unsigned i = 0; i < count; ++i) {
        fdmap[i] = fds[i] >= 0 ? fds[i] : SPAWN_FDCLOSED;
    }
    struct inheritance inherit;
    memset(&inherit, 0, sizeof(inherit));
    inherit.flags = SPAWN_SETSIGDEF | SPAWN_SETSIGMASK | SPAWN_SETGROUP;
    inherit.pgroup = SPAWN_NEWPGROUP;
    sigfillset(&inherit.sigdefault);
    sigemptyset(&inherit.sigmask);
    struct sched_param current;
    const int minimum = sched_get_priority_min(SCHED_OTHER);
    if (sandbox->lowerPriority && minimum >= 0 && sched_getparam(0, &current) == 0) {
        inherit.flags |= SPAWN_EXPLICIT_SCHED;
        inherit.policy = SCHED_OTHER;
        inherit.param.sched_priority =
            current.sched_priority - 2 > minimum ? current.sched_priority - 2 : minimum;
    }
    const pid_t child =
        ::spawn(path, static_cast<int>(count), fdmap, &inherit, argv, env ? env : empty);
    if (child < 0) {
        return errno ? errno : EIO;
    }
    *pid = child;
    return 0;
}

#else

namespace {

bool firstUse(const int *fds, unsigned index) {
    for (unsigned i = 0; i < index; ++i) {
        if (fds[i] == fds[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

int spawnChild(const char *path, char *const argv[], char *const env[], const int *fds,
               unsigned count, pid_t *pid) {
    if (!path || !argv || !env || !fds || !pid || !count || count > kSpawnDescriptorsMax) {
        return EINVAL;
    }
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        return error;
    }
    for (unsigned i = 0; i < count && !error; ++i) {
        if (fds[i] >= 0) {
            error = posix_spawn_file_actions_adddup2(&actions, fds[i], static_cast<int>(i));
        }
    }
    // Copies above the map are closed once mapped; those inside it were replaced.
    for (unsigned i = 0; i < count && !error; ++i) {
        if (fds[i] >= static_cast<int>(count) && firstUse(fds, i)) {
            error = posix_spawn_file_actions_addclose(&actions, fds[i]);
        }
    }
    if (!error) {
        error = posix_spawn(pid, path, &actions, NULL, argv, env);
    }
    posix_spawn_file_actions_destroy(&actions);
    return error;
}

#endif

} // namespace process
} // namespace nbt
