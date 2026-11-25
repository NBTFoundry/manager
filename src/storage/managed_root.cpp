// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/managed_root.hpp"

#include "nbt/fs.h"
#include "nbt/storage/path_open.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

core::Status initRoot(nbt_fs *fs, const char *root) {
    struct stat st;
    if (!fs || !root || root[0] != '/' || strlen(root) >= sizeof(fs->root) || lstat(root, &st) ||
        !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077)) {
        errno = EPERM;
        return core::Status::error(EPERM, "managed root must be an owned, private directory");
    }
#ifndef NBT_TESTING
    if (strcmp(root, "/var/opt/nbt-manager")) {
        errno = EPERM;
        return core::Status::error(EPERM, "managed root is outside the provisioned location");
    }
#endif
#ifdef __QNX__
    {
        struct statvfs v;
        if (statvfs(root, &v) || strcmp(v.f_basetype, "qnx6") ||
            (v.f_flag & (ST_RDONLY | ST_NOEXEC))) {
            errno = ENOTSUP;
            return core::Status::error(ENOTSUP,
                                       "managed root requires writable executable QNX6 storage");
        }
    }
#endif
    memcpy(fs->root, root, strlen(root) + 1);
    fs->device = st.st_dev;
    fs->inode = st.st_ino;
    fs->owner = st.st_uid;
    if (SafePathOpen::privateDir(fs, fs->root, true)) {
        return core::Status::fromErrno("managed root changed");
    }
    return core::Status();
}

core::Result<core::UniqueFd> checkedFd(bool validRoot, int fd, const char *errMsg) {
    if (!validRoot) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::error(EINVAL, "managed root is not initialized"));
    }
    if (fd < 0) {
        return core::Result<core::UniqueFd>::failure(core::Status::fromErrno(errMsg));
    }
    return core::Result<core::UniqueFd>::success(core::UniqueFd(fd));
}

} // namespace

ManagedRoot::ManagedRoot() : root_(), valid_(false) {
}

core::Status ManagedRoot::initialize(const char *root) {
    valid_ = false;
    const core::Status status = initRoot(&root_, root);
    if (!status.ok()) {
        return status;
    }
    valid_ = true;
    return core::Status();
}

core::Status ManagedRoot::path(PathKind kind, const char *app, const char *tx,
                               core::Span<char> output) const {
    if (!valid_ || !output.data() || output.size() > static_cast<size_t>(UINT_MAX)) {
        return core::Status::error(EINVAL, "invalid managed root or path output");
    }
    if (nbt_fs_path(const_cast<nbt_fs *>(&root_), kind, app, tx, output.data(),
                    static_cast<unsigned>(output.size()))) {
        return core::Status::fromErrno("managed path resolution failed");
    }
    return core::Status();
}

core::Result<core::UniqueFd> ManagedRoot::open(PathKind kind, const char *app, const char *tx,
                                               OpenMode mode) const {
    const int fd = valid_ ? nbt_fs_open(const_cast<nbt_fs *>(&root_), kind, app, tx, mode) : -1;
    return checkedFd(valid_, fd, "managed file open failed");
}

core::Result<core::UniqueFd> ManagedRoot::lease(const char *app) const {
    const int fd = valid_ ? nbt_fs_lease_exclusive(const_cast<nbt_fs *>(&root_), app) : -1;
    return checkedFd(valid_, fd, "application lease failed");
}

core::Result<core::UniqueFd> ManagedRoot::managerOperationLock() const {
    const int fd = valid_ ? nbt_fs_manager_operation_lock(const_cast<nbt_fs *>(&root_)) : -1;
    return checkedFd(valid_, fd, "manager operation lock failed");
}

} // namespace storage
} // namespace nbt

int nbt_fs_init(nbt_fs *fs, const char *root, char *error, unsigned error_size) {
    const nbt::core::Status status = nbt::storage::initRoot(fs, root);
    if (!status.ok()) {
        if (error && error_size) {
            snprintf(error, error_size, "%s", status.message());
        }
        errno = status.code();
        return -1;
    }
    if (error && error_size) {
        *error = 0;
    }
    return 0;
}

int nbt_fs_path(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx, char *out,
                unsigned size) {
    char relative[NBT_PATH_MAX];
    char walk[NBT_PATH_MAX];
    if (!fs || !out || !nbt::storage::SafePathOpen::validApp(app) ||
        nbt::storage::SafePathOpen::privateDir(fs, fs->root, true)) {
        errno = EPERM;
        return -1;
    }
    const nbt::core::Status status = nbt::storage::PathPolicy::relative(
        kind, app, tx, nbt::core::Span<char>(relative, sizeof(relative)));
    if (!status.ok()) {
        errno = status.code();
        return -1;
    }
    if (snprintf(out, size, "%s/%s", fs->root, relative) >= (int)size ||
        snprintf(walk, sizeof(walk), "%s/%s", fs->root, relative) >= (int)sizeof(walk)) {
        errno = EINVAL;
        return -1;
    }
    char *p = walk + strlen(fs->root) + 1;
    while ((p = strchr(p, '/')) != NULL) {
        *p = 0;
        if (nbt::storage::SafePathOpen::privateDir(fs, walk, false)) {
            return -1;
        }
        *p++ = '/';
    }
    return 0;
}

int nbt_fs_app_data(nbt_fs *fs, const char *app, char out[NBT_PATH_MAX]) {
    if (nbt_fs_path(fs, NBT_FS_DATA_DIR, app, NULL, out, NBT_PATH_MAX) ||
        nbt::storage::SafePathOpen::privateDir(fs, out, false)) {
        return -1;
    }
    return 0;
}

int nbt_fs_rename(nbt_fs *fs, nbt_fs_path_kind from, nbt_fs_path_kind to, const char *app,
                  const char *tx) {
    char src[NBT_PATH_MAX];
    char dst[NBT_PATH_MAX];
    if (!nbt::storage::PathPolicy::canRename(from, to, app)) {
        errno = EPERM;
        return -1;
    }
    if (nbt_fs_path(fs, from, app, tx, src, sizeof(src)) ||
        nbt_fs_path(fs, to, app, tx, dst, sizeof(dst)) ||
        nbt::storage::SafePathOpen::safeExisting(fs, src)) {
        return -1;
    }
    if (!nbt::storage::SafePathOpen::safeExisting(fs, dst)) {
        if (!nbt::storage::PathPolicy::canReplace(from, to)) {
            errno = EEXIST;
            return -1;
        }
    } else if (errno != ENOENT) {
        return -1;
    }
    return rename(src, dst);
}

int nbt_fs_unlink(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx) {
    char path[NBT_PATH_MAX];
    if (!nbt::storage::PathPolicy::canUnlink(kind)) {
        errno = EPERM;
        return -1;
    }
    if (nbt_fs_path(fs, kind, app, tx, path, sizeof(path)) ||
        nbt::storage::SafePathOpen::safeExisting(fs, path)) {
        return -1;
    }
    return unlink(path);
}

int nbt_fs_exists(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx) {
    char path[NBT_PATH_MAX];
    if (nbt_fs_path(fs, kind, app, tx, path, sizeof(path))) {
        return -1;
    }
    if (!nbt::storage::SafePathOpen::safeExisting(fs, path)) {
        return 1;
    }
    return errno == ENOENT ? 0 : -1;
}

int nbt_fs_lease_exclusive(nbt_fs *fs, const char *app) {
    return nbt::storage::SafePathOpen::lockNonBlocking(fs, NBT_FS_LEASE, app);
}

int nbt_fs_service_lease_exclusive(nbt_fs *fs, const char *app) {
    return nbt::storage::SafePathOpen::lockNonBlocking(fs, NBT_FS_SERVICE_LEASE, app);
}

int nbt_fs_manager_operation_lock(nbt_fs *fs) {
    return nbt::storage::SafePathOpen::lockNonBlocking(fs, NBT_FS_MANAGER_OPERATION_LOCK,
                                                       "manager");
}

int nbt_fs_manager_operation_parent(nbt_fs *fs) {
    struct flock lock;
    const pid_t parent = getppid();
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_OPERATION_LOCK, "manager", NULL, NBT_FS_READ);
    if (fd < 0) {
        return -1;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (parent <= 1 || fcntl(fd, F_GETLK, &lock) || lock.l_type == F_UNLCK ||
        lock.l_pid != parent) {
        close(fd);
        errno = EBUSY;
        return -1;
    }
    return fd;
}

int nbt_fs_list_apps(nbt_fs *fs, char names[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1]) {
    char path[NBT_PATH_MAX];
    int count = 0;
    int result = -1;
    if (nbt_fs_path(fs, NBT_FS_PROFILE, "placeholder", NULL, path, sizeof(path))) {
        return -1;
    }
    char *last = strrchr(path, '/');
    if (!last) {
        errno = EINVAL;
        return -1;
    }
    *last = 0;
    DIR *directory = opendir(path);
    if (!directory) {
        return -1;
    }
    errno = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        const size_t n = strlen(entry->d_name);
        char name[NBT_APP_NAME_MAX + 1];
        if (n <= 5 || n > NBT_APP_NAME_MAX + 5 || strcmp(entry->d_name + n - 5, ".conf")) {
            continue;
        }
        memcpy(name, entry->d_name, n - 5);
        name[n - 5] = 0;
        if (!nbt::storage::SafePathOpen::validApp(name) || !strcmp(name, "manager")) {
            continue;
        }
        const int fd = nbt_fs_open(fs, NBT_FS_PROFILE, name, NULL, NBT_FS_READ);
        if (fd < 0) {
            result = -1;
            goto done;
        }
        close(fd);
        if (count == NBT_APP_LIST_MAX) {
            errno = ENOSPC;
            goto done;
        }
        int i = count;
        for (; i > 0 && strcmp(names[i - 1], name) > 0; --i) {
            memcpy(names[i], names[i - 1], sizeof(names[i]));
        }
        memcpy(names[i], name, strlen(name) + 1);
        ++count;
        errno = 0;
    }
    if (!errno) {
        result = count;
    }
done:
    closedir(directory);
    return result;
}
