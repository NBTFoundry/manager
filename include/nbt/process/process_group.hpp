// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_PROCESS_GROUP_HPP
#define NBT_PROCESS_PROCESS_GROUP_HPP

#include "nbt/fs.h"
#include "nbt/process.h"
#include "nbt/process/child_spawn.hpp"

#include <stdint.h>

namespace nbt {
namespace process {

class ProcessGroup {
public:
    static int start(nbt_fs *fs, const char *app, nbt_fs_path_kind kind, const char *slot,
                     char *const *environment, int statusFd, int mostFd, nbt_process *child,
                     uint64_t nowMs, int gamepadFd = -1, int controllerFd = -1);
    /* `outputFd` becomes the child's stdout and stderr; -1 passes the caller's own. */
    static int start(nbt_fs *fs, const char *app, nbt_fs_path_kind kind, const char *slot,
                     char *const *environment, int statusFd, int mostFd, int outputFd,
                     const ChildSandbox *sandbox, nbt_process *child, uint64_t nowMs,
                     int gamepadFd = -1, int controllerFd = -1);
    static int poll(nbt_process *child, uint64_t nowMs);
    static int healthy(const nbt_process *child, uint64_t nowMs);
    static int signal(const nbt_process *child, int sig);
    static int stop(nbt_process *child);
    static int terminate(nbt_process *child, uint64_t nowMs);
    static int reap(nbt_process *child, uint64_t nowMs);

private:
    static int duplicatePrivate(int fd, bool closeOnExec);
    static void closeHandles(nbt_process *child);
    static int hasLease(const nbt_process *child);
    /* Reaps the child if it has exited, trying `tries` times 20 ms apart:
     * 0 reaped (exit recorded), 1 still running, -1 on error. */
    static int ended(nbt_process *child, unsigned tries);
};

} // namespace process
} // namespace nbt

#endif
