#ifndef NBT_PROCESS_CHILD_SPAWN_HPP
#define NBT_PROCESS_CHILD_SPAWN_HPP

#include "nbt/fs.h"
#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace process {

static const unsigned kSpawnDescriptorsMax = 9;

struct ChildSandbox {
    bool isolate;
    bool lowerPriority;
    uint64_t memoryBytes;
    uint64_t addressBytes;
    uint64_t fileBytes;
    unsigned maxFiles;
    unsigned maxThreads;
    bool noCore;
    int dropUid;
    int dropGid;
    const char *chrootDir;
};

ChildSandbox defaultAppSandbox();
void buildProcessArgv(char *argv[16], char *path, nbt_fs_path_kind kind, const char *slot,
                      const char **executable);

/* Starts `path` the way NBT-AAD's iDrive app starts its daemon on QNX: spawn()
 * with a descriptor map (the child's descriptor i is the caller's fds[i]; -1
 * leaves it closed), default signal actions and an empty signal mask. Other
 * systems (host tests) use posix_spawn with the same map. Returns 0, or an
 * errno value. */
int spawnChild(const char *path, char *const argv[], char *const env[], const int *fds,
               unsigned count, pid_t *pid);

/* Same descriptor/signal contract as spawnChild, plus resource limits and a
 * lowered CPU priority; on the host build, an optional privilege/chroot drop.
 * The car applies the limits through the --app-exec wrapper
 * (sandbox_limits.hpp). */
int spawnSandboxedChild(const char *path, char *const argv[], char *const env[], const int *fds,
                        unsigned count, const ChildSandbox *sandbox, pid_t *pid);

} // namespace process
} // namespace nbt

#endif
