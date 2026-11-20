// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/door.hpp"
#include "nbt/core/system.hpp"
#include "nbt/launcher/slot_store.hpp"
#include "nbt/manager_stage.h"
#include "nbt/process/child_spawn.hpp"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern "C" char **environ;
namespace nbt {
namespace launcher {
namespace {

const uint64_t kAutostartDispatchMs = 15000; // the worker starts hosts and returns
const unsigned kAutostartLockTries = 40;     // 10 s for the manager operation lock

/* The known-good worker, verified: the active slot, or the previous one while
 * a manager update is pending, so that a boot-time job never runs (and so
 * never confirms) a candidate. The caller holds the manager operation lock. */
int knownGoodWorker(nbt_fs *fs, const storage::ManagedRoot &root, char *path, unsigned size) {
    SlotStore store(root);
    nbt_manager_state state;
    if (!store.load(state).ok()) {
        return 1;
    }
    const nbt_manager_slot &slot = state.pending ? state.previous : state.active;
    if (nbt_manager_verify_slot(fs, &slot)) {
        return 1;
    }
    return nbt_fs_path(fs, NBT_FS_MANAGER_SLOT_WORKER, "manager", slot.slot ? "1" : "0", path, size)
               ? 1
               : 0;
}

/* Starts it in `mode` (argv[1]); with `readyFd` it also gets --ready-fd 3. */
int spawnWorker(nbt_fs *fs, char *path, const char *mode, int readyFd, pid_t *pid) {
    char *argv[] = {path, const_cast<char *>(mode), NULL, NULL, NULL, NULL, NULL};
    unsigned argc = 2;
    if (readyFd >= 0) {
        argv[argc++] = const_cast<char *>("--ready-fd");
        argv[argc++] = const_cast<char *>("3");
    }
    const char *executable = path;
#ifdef NBT_TESTING
    if (getenv("NBT_TEST_MANAGER_WORKER")) {
        executable = argv[0] = getenv("NBT_TEST_MANAGER_WORKER");
        argv[argc++] = const_cast<char *>("--root");
        argv[argc++] = fs->root;
    }
#else
    (void)fs;
#endif
    const int fds[] = {0, 1, 2, readyFd};
    return process::spawnChild(executable, argv, environ, fds, readyFd >= 0 ? 4u : 3u, pid) ? 1 : 0;
}

void killAndReap(pid_t pid) {
    (void)kill(pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

} // namespace

int startDoor(nbt_fs *fs) {
    storage::ManagedRoot root(*fs);
    core::Result<core::UniqueFd> operation = root.managerOperationLock();
    char path[NBT_PATH_MAX];
    if (!operation.ok() || knownGoodWorker(fs, root, path, sizeof(path))) {
        return 1;
    }
    int ends[2];
    if (pipe(ends)) {
        return 1;
    }
    core::UniqueFd reader(ends[0]), writer(ends[1]);
    if (fcntl(reader.get(), F_SETFD, FD_CLOEXEC) || fcntl(writer.get(), F_SETFD, FD_CLOEXEC)) {
        return 1;
    }
    pid_t pid = 0;
    if (spawnWorker(fs, path, "--door", writer.get(), &pid)) {
        return 1;
    }
    writer.reset();
    core::Result<uint64_t> now = core::monotonicMs();
    char ready[6];
    size_t read = 0;
    const bool ok =
        now.ok() &&
        core::readWithDeadline(
            reader.get(), core::Span<uint8_t>(reinterpret_cast<uint8_t *>(ready), sizeof(ready)),
            now.value() + 5000, &read)
            .ok() &&
        read == sizeof(ready) && !memcmp(ready, "READY\n", sizeof(ready));
    if (!ok) {
        killAndReap(pid);
        return 1;
    }
    // The verified worker has executed and initialized. It now owns its own
    // lifetime lock; releasing the operation lock permits manager slot updates.
    return 0;
}

int runAutostart(nbt_fs *fs) {
    storage::ManagedRoot root(*fs);
    // Taken without waiting: the boot's door job may hold it for a few seconds.
    core::Result<core::UniqueFd> operation = root.managerOperationLock();
    for (unsigned tries = 0; !operation.ok() && tries < kAutostartLockTries; ++tries) {
        const struct timespec step = {0, 250 * 1000 * 1000};
        (void)nanosleep(&step, NULL);
        operation = root.managerOperationLock();
    }
    char path[NBT_PATH_MAX];
    pid_t pid = 0;
    core::Result<uint64_t> start = core::monotonicMs();
    if (!operation.ok() || !start.ok() || knownGoodWorker(fs, root, path, sizeof(path)) ||
        spawnWorker(fs, path, "--autostart", -1, &pid)) {
        return 1;
    }
    // The operation lock is held until the dispatch has ended: a manager
    // update cannot replace the worker that is starting applications.
    for (;;) {
        int status = 0;
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
        }
        core::Result<uint64_t> now = core::monotonicMs();
        if ((done < 0 && errno != EINTR) || !now.ok() ||
            now.value() - start.value() >= kAutostartDispatchMs) {
            killAndReap(pid);
            return 1;
        }
        const struct timespec step = {0, 50 * 1000 * 1000};
        (void)nanosleep(&step, NULL);
    }
}

} // namespace launcher
} // namespace nbt
