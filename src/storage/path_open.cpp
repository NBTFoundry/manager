// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/path_open.hpp"

#include "nbt/package.h"
#include "nbt/storage/path_policy.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

gid_t trustedGroup() {
#ifdef NBT_TESTING
    const char *value = getenv("NBT_TEST_HMI_TRUSTED_GID");
    if (value) {
        return (gid_t)strtoul(value, NULL, 10);
    }
#endif
    return 0;
}

} // namespace

bool SafePathOpen::validApp(const char *app) {
    return nbt_token_valid(app, NBT_ID_MAX, 0) != 0;
}

bool SafePathOpen::carModeSafe(const struct stat *st) {
    return !(st->st_mode & 0002) && (!(st->st_mode & 0020) || st->st_gid == trustedGroup());
}

bool SafePathOpen::volumeHasSpace(const struct statvfs &volume, uint64_t required) {
    if (!volume.f_frsize || (volume.f_flag & ST_RDONLY) ||
        required > UINT64_MAX - volume.f_frsize) {
        return false;
    }
    const uint64_t blocks = required / volume.f_frsize + (required % volume.f_frsize != 0) + 8;
    return blocks >= 8 && volume.f_bavail >= blocks;
}

int SafePathOpen::checkOwnedDir(dev_t device, uid_t owner, ino_t rootInode, const char *path,
                                bool isRoot, bool privateOnly) {
    struct stat st;
    if (lstat(path, &st)) {
        return -1;
    }
    const bool modeOk = privateOnly ? !(st.st_mode & 0022) : carModeSafe(&st);
    if (!S_ISDIR(st.st_mode) || st.st_dev != device || st.st_uid != owner || !modeOk ||
        (isRoot && st.st_ino != rootInode)) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int SafePathOpen::checkOwnedFile(dev_t device, uid_t owner, const char *path) {
    struct stat st;
    if (lstat(path, &st)) {
        return -1;
    }
    if (!S_ISREG(st.st_mode) || st.st_dev != device || st.st_uid != owner || st.st_nlink != 1 ||
        (st.st_mode & 0022)) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int SafePathOpen::openOwnedFile(dev_t device, uid_t owner, const char *path, int flags,
                                mode_t mode) {
    struct stat before;
    struct stat after;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path, flags, mode);
    if (fd < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) || checkOwnedFile(device, owner, path) ||
        lstat(path, &before) || fstat(fd, &after)) {
        const int saved = errno;
        close(fd);
        errno = saved ? saved : EPERM;
        return -1;
    }
    // Its own errno: a value left by an earlier call must not pass for the cause.
    if (before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
        close(fd);
        errno = ESTALE;
        return -1;
    }
    return fd;
}

int SafePathOpen::privateDir(const nbt_fs *fs, const char *path, bool isRoot) {
    return checkOwnedDir(fs->device, fs->owner, fs->inode, path, isRoot, true);
}

int SafePathOpen::checkedFile(const nbt_fs *fs, const char *path, int fd) {
    struct stat before;
    struct stat after;
    if (checkOwnedFile(fs->device, fs->owner, path) || lstat(path, &before) || fstat(fd, &after) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int SafePathOpen::safeExisting(const nbt_fs *fs, const char *path) {
    return checkOwnedFile(fs->device, fs->owner, path);
}

int SafePathOpen::openChecked(const nbt_fs *fs, const char *path, int flags, mode_t mode) {
    return openOwnedFile(fs->device, fs->owner, path, flags, mode);
}

int SafePathOpen::lockNonBlocking(nbt_fs *fs, nbt_fs_path_kind kind, const char *app) {
    struct flock lock;
    const int fd = nbt_fs_open(fs, kind, app, NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0) {
        return -1;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int SafePathOpen::syncWritableFd(int fd) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || (flags & O_ACCMODE) == O_RDONLY) {
        close(fd);
        errno = EBADF;
        return -1;
    }
    if (fsync(fd)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return close(fd);
}

int SafePathOpen::syncDirPath(const char *path) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    if (fsync(fd)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return close(fd);
}

int SafePathOpen::syncDirKinds(nbt_fs *fs, const char *app, const char *tx,
                               const nbt_fs_path_kind *kinds, size_t count) {
    char path[NBT_PATH_MAX];
    for (size_t i = 0; i < count; ++i) {
        if (nbt_fs_path(fs, kinds[i], app, tx, path, sizeof(path)) || syncDirPath(path)) {
            return -1;
        }
    }
    return 0;
}

} // namespace storage
} // namespace nbt

int nbt_fs_open(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx,
                nbt_fs_open_mode mode) {
    char path[NBT_PATH_MAX];
    int flags = 0;
    if (nbt_fs_path(fs, kind, app, tx, path, sizeof(path))) {
        return -1;
    }
    if (mode == NBT_FS_READ) {
        if (nbt::storage::SafePathOpen::safeExisting(fs, path)) {
            return -1;
        }
        flags = O_RDONLY;
    } else if (mode == NBT_FS_CREATE && nbt::storage::PathPolicy::canCreate(kind)) {
        flags = O_WRONLY | O_CREAT | O_EXCL;
    } else if (mode == NBT_FS_REWRITE && kind == NBT_FS_DATA_SETTINGS) {
        struct stat st;
        if (!nbt::storage::SafePathOpen::safeExisting(fs, path)) {
            if (lstat(path, &st) || (st.st_mode & 0077)) {
                errno = EPERM;
                return -1;
            }
            flags = O_WRONLY | O_NONBLOCK;
        } else if (errno == ENOENT) {
            flags = O_WRONLY | O_NONBLOCK | O_CREAT | O_EXCL;
        } else {
            return -1;
        }
    } else if (mode == NBT_FS_APPEND && kind == NBT_FS_MANAGER_LOG) {
        if (nbt::storage::SafePathOpen::safeExisting(fs, path)) {
            return -1;
        }
        flags = O_WRONLY | O_APPEND;
    } else if (mode == NBT_FS_APPEND && kind == NBT_FS_APP_LOG) {
        if (!nbt::storage::SafePathOpen::safeExisting(fs, path)) {
            flags = O_WRONLY | O_APPEND;
        } else if (errno == ENOENT) {
            flags = O_WRONLY | O_APPEND | O_CREAT | O_EXCL;
        } else {
            return -1;
        }
    } else if (mode == NBT_FS_OPEN_LOCK && nbt::storage::PathPolicy::canLock(kind)) {
        if (!nbt::storage::SafePathOpen::safeExisting(fs, path)) {
            flags = O_RDWR;
        } else if (errno == ENOENT) {
            flags = O_RDWR | O_CREAT | O_EXCL;
        } else {
            return -1;
        }
    } else {
        errno = EPERM;
        return -1;
    }
    return nbt::storage::SafePathOpen::openChecked(fs, path, flags, 0600);
}

int nbt_fs_barrier(nbt_fs *fs, const char *app, const char *tx) {
    const int fd = nbt_fs_open(fs, NBT_FS_SYNC, app, NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0 || nbt::storage::SafePathOpen::syncWritableFd(fd)) {
        return -1;
    }
#ifndef __QNX__
    {
        const nbt_fs_path_kind kinds[] = {NBT_FS_APP_DIR, NBT_FS_STATE_DIR, NBT_FS_TX_DIR};
        const size_t count = tx ? 3 : 2;
        if (nbt::storage::SafePathOpen::syncDirKinds(fs, app, tx, kinds, count)) {
            return -1;
        }
    }
#else
    (void)tx;
#endif
    return 0;
}

int nbt_fs_create_tx(nbt_fs *fs, const char *app, const char *tx) {
    char path[NBT_PATH_MAX];
    if (nbt_fs_path(fs, NBT_FS_TX_DIR, app, tx, path, sizeof(path))) {
        return -1;
    }
    return mkdir(path, 0700);
}

int nbt_fs_random_tx(char out[33]) {
    unsigned char bytes[16];
    if (nbt_random(bytes, sizeof(bytes))) {
        return -1;
    }
    return nbt_hex(bytes, sizeof(bytes), out, 33);
}
