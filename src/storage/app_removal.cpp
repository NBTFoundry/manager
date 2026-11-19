#include "nbt/fs.h"

#include "nbt/storage/path_open.hpp"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

/* Deeper trees under an application's data are refused rather than walked. */
const unsigned kDepthMax = 16;

const nbt_fs_path_kind kTrees[] = {NBT_FS_PROVISION_APP_ROOT, NBT_FS_STATE_DIR, NBT_FS_DATA_DIR};

/* Removes `path` (`length` bytes in a NBT_PATH_MAX buffer, restored on
 * return) and everything under it. A link is unlinked, never entered; a
 * directory on another device is refused. The application is stopped and its
 * directories are private (0700), so nothing else is expected to change the
 * tree meanwhile. Entries are removed in passes until a pass finds none, as
 * removing while reading a directory may skip entries. */
int removeTree(char *path, size_t length, dev_t device, unsigned depth) {
    struct stat st;
    if (lstat(path, &st)) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) && errno != ENOENT ? -1 : 0;
    }
    if (st.st_dev != device || depth > kDepthMax) {
        errno = st.st_dev != device ? EXDEV : ELOOP;
        return -1;
    }
    for (bool removed = true; removed;) {
        removed = false;
        DIR *dir = opendir(path);
        if (!dir) {
            return -1;
        }
        int result = 0;
        errno = 0;
        for (struct dirent *entry; (entry = readdir(dir)) != NULL; errno = 0) {
            const char *name = entry->d_name;
            const size_t n = strlen(name);
            if (!strcmp(name, ".") || !strcmp(name, "..")) {
                continue;
            }
            if (length + 1 + n >= NBT_PATH_MAX) {
                errno = ENAMETOOLONG;
                result = -1;
                break;
            }
            path[length] = '/';
            memcpy(path + length + 1, name, n + 1);
            result = removeTree(path, length + 1 + n, device, depth + 1);
            path[length] = 0;
            if (result) {
                break;
            }
            removed = true;
        }
        const int saved = errno;
        closedir(dir);
        if (result || saved) {
            errno = saved ? saved : EIO;
            return -1;
        }
    }
    return rmdir(path) && errno != ENOENT ? -1 : 0;
}

int removeFile(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx) {
    char path[NBT_PATH_MAX];
    if (nbt_fs_path(fs, kind, app, tx, path, sizeof(path))) {
        return -1;
    }
    if (SafePathOpen::safeExisting(fs, path)) {
        return errno == ENOENT ? 0 : -1;
    }
    return unlink(path) && errno != ENOENT ? -1 : 0;
}

/* Makes the removals durable: on QNX an fsync commits the filesystem, so the
 * manager's own sync file serves; elsewhere the parent directories are synced. */
int syncRemoval(nbt_fs *fs) {
    const int fd = nbt_fs_open(fs, NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0 || SafePathOpen::syncWritableFd(fd)) {
        return -1;
    }
#ifndef __QNX__
    const nbt_fs_path_kind dirs[] = {NBT_FS_SETUP_APPS_DIR, NBT_FS_SETUP_STATE_DIR,
                                     NBT_FS_SETUP_DATA_DIR, NBT_FS_SETUP_APP_PROFILES_DIR,
                                     NBT_FS_SETUP_APP_KEYS_DIR};
    return SafePathOpen::syncDirKinds(fs, "manager", NULL, dirs, sizeof(dirs) / sizeof(dirs[0]));
#else
    return 0;
#endif
}

} // namespace
} // namespace storage
} // namespace nbt

int nbt_fs_app_present(nbt_fs *fs, const char *app) {
    static const nbt_fs_path_kind kKinds[] = {NBT_FS_DATA_DIR, NBT_FS_PROVISION_APP_ROOT,
                                              NBT_FS_STATE_DIR, NBT_FS_PROFILE, NBT_FS_KEY};
    char path[NBT_PATH_MAX];
    struct stat st;
    for (unsigned i = 0; i < sizeof(kKinds) / sizeof(kKinds[0]); ++i) {
        if (nbt_fs_path(fs, kKinds[i], app, kKinds[i] == NBT_FS_KEY ? "0" : NULL, path,
                        sizeof(path))) {
            return -1;
        }
        if (!lstat(path, &st)) {
            return 1;
        }
        if (errno != ENOENT) {
            return -1;
        }
    }
    return 0;
}

int nbt_fs_app_remove(nbt_fs *fs, const char *app) {
    using nbt::storage::SafePathOpen;
    char path[NBT_PATH_MAX];
    char key[2] = "0";
    if (!fs || !SafePathOpen::validApp(app) || !strcmp(app, "manager")) {
        errno = EPERM;
        return -1;
    }
    if (nbt::storage::removeFile(fs, NBT_FS_PROFILE, app, NULL) || nbt::storage::syncRemoval(fs)) {
        return -1;
    }
    for (; key[0] <= '7'; ++key[0]) {
        if (nbt::storage::removeFile(fs, NBT_FS_KEY, app, key)) {
            return -1;
        }
    }
    for (size_t i = 0; i < sizeof(nbt::storage::kTrees) / sizeof(nbt::storage::kTrees[0]); ++i) {
        if (nbt_fs_path(fs, nbt::storage::kTrees[i], app, NULL, path, sizeof(path))) {
            return -1;
        }
        if (SafePathOpen::privateDir(fs, path, false)) {
            if (errno == ENOENT) {
                continue;
            }
            return -1;
        }
        if (nbt::storage::removeTree(path, strlen(path), fs->device, 0)) {
            return -1;
        }
    }
    return nbt::storage::syncRemoval(fs);
}
