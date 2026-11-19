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
#include <unistd.h>
extern "C" char **environ;
namespace nbt {
namespace launcher {
int startDoor(nbt_fs *fs) {
    storage::ManagedRoot root(*fs);
    core::Result<core::UniqueFd> operation = root.managerOperationLock();
    if (!operation.ok()) {
        return 1;
    }
    SlotStore store(root);
    nbt_manager_state state;
    if (!store.load(state).ok()) {
        return 1;
    }
    const nbt_manager_slot &slot = state.pending ? state.previous : state.active;
    if (nbt_manager_verify_slot(fs, &slot)) {
        return 1;
    }
    char path[NBT_PATH_MAX];
    if (nbt_fs_path(fs, NBT_FS_MANAGER_SLOT_WORKER, "manager", slot.slot ? "1" : "0", path,
                    sizeof(path))) {
        return 1;
    }
    char *argv[] = {path,
                    const_cast<char *>("--door"),
                    const_cast<char *>("--ready-fd"),
                    const_cast<char *>("3"),
                    NULL,
                    NULL,
                    NULL};
    const char *executable = path;
#ifdef NBT_TESTING
    if (getenv("NBT_TEST_MANAGER_WORKER")) {
        executable = argv[0] = getenv("NBT_TEST_MANAGER_WORKER");
        argv[4] = const_cast<char *>("--root");
        argv[5] = fs->root;
    }
#endif
    int ends[2];
    if (pipe(ends)) {
        return 1;
    }
    core::UniqueFd reader(ends[0]), writer(ends[1]);
    if (fcntl(reader.get(), F_SETFD, FD_CLOEXEC) || fcntl(writer.get(), F_SETFD, FD_CLOEXEC)) {
        return 1;
    }
    const int fds[] = {0, 1, 2, writer.get()};
    pid_t pid = 0;
    if (process::spawnChild(executable, argv, environ, fds, 4, &pid)) {
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
        (void)kill(pid, SIGKILL);
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        return 1;
    }
    // The verified worker has executed and initialized. It now owns its own
    // lifetime lock; releasing the operation lock permits manager slot updates.
    return 0;
}
} // namespace launcher
} // namespace nbt
