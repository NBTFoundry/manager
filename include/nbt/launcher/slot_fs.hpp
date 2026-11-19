#ifndef NBT_LAUNCHER_SLOT_FS_HPP
#define NBT_LAUNCHER_SLOT_FS_HPP

#include "nbt/fs.h"
#include "nbt/manager_state.h"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace launcher {

class SlotFs {
public:
    static int fail(char *error, unsigned size, const char *message);
    static int writeAll(int fd, const void *data, size_t size);
    static int copyStream(int inputFd, int outputFd, uint64_t length, bool injectReleaseWrite);
    static int requireSpace(const char *root, uint64_t need, uint64_t reserveBlocks);
    static int managerBarrier(nbt_fs *fs);
    static int slotBarrier(nbt_fs *fs, const char *slot);
    static int fileMatches(nbt_fs *fs, nbt_fs_path_kind kind, const char *slot, uint64_t length,
                           const unsigned char digest[32], bool executable);
    static int preflightFile(nbt_fs *fs, nbt_fs_path_kind kind, uint64_t length,
                             const unsigned char digest[32], bool executable);
    static int readState(nbt_fs *fs, nbt_manager_state *state);
    static int writeState(nbt_fs *fs, const nbt_manager_state *state);
    static void stageFault(const char *step);
    static int injectedStageError(const char *step);
};

} // namespace launcher
} // namespace nbt

#endif
