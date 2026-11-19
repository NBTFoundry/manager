#ifndef NBT_STORAGE_PATH_OPEN_HPP
#define NBT_STORAGE_PATH_OPEN_HPP

#include "nbt/fs.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

namespace nbt {
namespace storage {

class SafePathOpen {
public:
    static bool validApp(const char *app);
    static bool carModeSafe(const struct stat *st);
    static bool volumeHasSpace(const struct statvfs &volume, uint64_t required);
    static int checkOwnedDir(dev_t device, uid_t owner, ino_t rootInode, const char *path,
                             bool isRoot, bool privateOnly);
    static int checkOwnedFile(dev_t device, uid_t owner, const char *path);
    static int openOwnedFile(dev_t device, uid_t owner, const char *path, int flags, mode_t mode);
    static int privateDir(const nbt_fs *fs, const char *path, bool isRoot);
    static int checkedFile(const nbt_fs *fs, const char *path, int fd);
    static int safeExisting(const nbt_fs *fs, const char *path);
    static int openChecked(const nbt_fs *fs, const char *path, int flags, mode_t mode);
    static int lockNonBlocking(nbt_fs *fs, nbt_fs_path_kind kind, const char *app);
    static int syncWritableFd(int fd);
    static int syncDirPath(const char *path);
    static int syncDirKinds(nbt_fs *fs, const char *app, const char *tx,
                            const nbt_fs_path_kind *kinds, size_t count);
};

} // namespace storage
} // namespace nbt

#endif
