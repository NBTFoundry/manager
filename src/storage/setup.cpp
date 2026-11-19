// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/setup.hpp"

#include "nbt/core/fault.hpp"
#include "nbt/storage/path_open.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

const nbt_fs_path_kind kSetupDirs[] = {
    NBT_FS_SETUP_APPS_DIR,          NBT_FS_SETUP_DATA_DIR,    NBT_FS_SETUP_STATE_DIR,
    NBT_FS_SETUP_MANAGER_STATE_DIR, NBT_FS_SETUP_TRUST_DIR,   NBT_FS_SETUP_APP_PROFILES_DIR,
    NBT_FS_SETUP_APP_KEYS_DIR,      NBT_FS_SETUP_MANAGER_DIR, NBT_FS_SETUP_SLOTS_DIR,
    NBT_FS_SETUP_SLOT0_DIR,         NBT_FS_SETUP_SLOT1_DIR};

core::Status call(int result, const char *message) {
    return result ? core::Status::fromErrno(message) : core::Status();
}

int setupRefuse(char *error, unsigned errorSize, const char *reason) {
    if (error && errorSize) {
        snprintf(error, errorSize, "manager storage: %s", reason);
    }
    errno = EPERM;
    return -1;
}

bool setupNextKind(nbt_fs_path_kind kind) {
    return kind == NBT_FS_SETUP_KEY_NEXT || kind == NBT_FS_SETUP_PROFILE_NEXT ||
           kind == NBT_FS_SETUP_LAUNCHER_NEXT;
}

const char *checkSetupVolume(const struct statvfs &volume, uint64_t required) {
    if (volume.f_flag & ST_RDONLY) {
        return "filesystem is read-only";
    }
#ifdef ST_NOEXEC
    if (volume.f_flag & ST_NOEXEC) {
        return "filesystem is mounted noexec";
    }
#endif
#ifdef __QNX__
    if (strcmp(volume.f_basetype, "qnx6")) {
        return "filesystem is not qnx6";
    }
#endif
    if (!SafePathOpen::volumeHasSpace(volume, required) ||
        NBT_TEST_FLAG("NBT_TEST_SETUP_NO_SPACE")) {
        return "not enough space";
    }
    return NULL;
}

} // namespace

core::Status SetupLayout::createRoot(ManagedRoot *root, const char *path, uint64_t required) {
    char error[128];
    if (!root) {
        return core::Status::error(EINVAL, "missing managed root");
    }
    if (nbt_fs_setup_root(root->legacy(), path, required, error, sizeof(error))) {
        return core::Status::fromErrno("manager storage setup failed");
    }
    return root->initialize(path);
}

core::Status SetupLayout::create(const ManagedRoot &root) {
    return call(nbt_fs_setup_layout(const_cast<nbt_fs *>(root.legacy())), "setup layout failed");
}

core::Result<core::UniqueFd> SetupLayout::createFile(const ManagedRoot &root, PathKind next) {
    const int fd = nbt_fs_setup_create(const_cast<nbt_fs *>(root.legacy()), next);
    if (fd < 0) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("setup file creation failed"));
    }
    return core::Result<core::UniqueFd>::success(core::UniqueFd(fd));
}

core::Status SetupLayout::publish(const ManagedRoot &root, PathKind next) {
    return call(nbt_fs_setup_publish(const_cast<nbt_fs *>(root.legacy()), next),
                "setup publish failed");
}

core::Status SetupLayout::discard(const ManagedRoot &root, PathKind next) {
    return call(nbt_fs_setup_discard(const_cast<nbt_fs *>(root.legacy()), next),
                "setup discard failed");
}

core::Status SetupLayout::sync(const ManagedRoot &root) {
    return call(nbt_fs_setup_sync(const_cast<nbt_fs *>(root.legacy())), "setup sync failed");
}

} // namespace storage
} // namespace nbt

FILE *nbt_fs_bootstrap_temp(void) {
    struct stat st;
    FILE *file = tmpfile();
    if (!file) {
        return NULL;
    }
    const int fd = fileno(file);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_nlink > 1 || fchmod(fd, 0600) || fcntl(fd, F_SETFD, FD_CLOEXEC)) {
        const int saved = errno;
        fclose(file);
        errno = saved ? saved : EPERM;
        return NULL;
    }
    return file;
}

int nbt_fs_setup_root(nbt_fs *fs, const char *root, uint64_t required, char *error,
                      unsigned error_size) {
    char parent[NBT_PATH_MAX];
    struct stat st;
    struct stat child;
    struct statvfs volume;
    if (!fs || !root || root[0] != '/' || strlen(root) >= sizeof(parent)) {
        return nbt::storage::setupRefuse(error, error_size, "invalid path");
    }
#ifndef NBT_TESTING
    if (strcmp(root, "/var/opt/nbt-manager")) {
        return nbt::storage::setupRefuse(error, error_size, "not /var/opt/nbt-manager");
    }
#endif
    memcpy(parent, root, strlen(root) + 1);
    char *slash = strrchr(parent, '/');
    if (!slash || !slash[1] || slash == parent || !strcmp(slash + 1, ".") ||
        !strcmp(slash + 1, "..")) {
        return nbt::storage::setupRefuse(error, error_size, "invalid path");
    }
    *slash = 0;
    if (stat(parent, &st) || !S_ISDIR(st.st_mode)) {
        return nbt::storage::setupRefuse(error, error_size, "parent folder missing");
    }
    if (st.st_uid != geteuid()) {
        return nbt::storage::setupRefuse(error, error_size, "parent folder owned by another user");
    }
    if (!nbt::storage::SafePathOpen::carModeSafe(&st)) {
        return nbt::storage::setupRefuse(error, error_size,
                                         "parent folder writable by other users");
    }
    if (statvfs(parent, &volume)) {
        return nbt::storage::setupRefuse(error, error_size, "filesystem cannot be queried");
    }
    if (const char *volErr = nbt::storage::checkSetupVolume(volume, required)) {
        return nbt::storage::setupRefuse(error, error_size, volErr);
    }
    if (lstat(root, &child)) {
        if (errno != ENOENT || mkdir(root, 0700)) {
            return nbt::storage::setupRefuse(error, error_size, "folder cannot be created");
        }
    } else if (!S_ISDIR(child.st_mode) || child.st_dev != st.st_dev || child.st_uid != geteuid() ||
               (child.st_mode & 0077)) {
        return nbt::storage::setupRefuse(error, error_size,
                                         "existing folder is not a private directory of this user");
    }
    if (nbt_fs_init(fs, root, NULL, 0) || fs->device != st.st_dev) {
        return nbt::storage::setupRefuse(error, error_size, "folder changed while checked");
    }
    return 0;
}

int nbt_fs_setup_create(nbt_fs *fs, nbt_fs_path_kind kind) {
    char path[NBT_PATH_MAX];
    if (!nbt::storage::setupNextKind(kind)) {
        errno = EPERM;
        return -1;
    }
    if (nbt_fs_path(fs, kind, "manager", NULL, path, sizeof(path))) {
        return -1;
    }
    return nbt::storage::SafePathOpen::openChecked(fs, path, O_WRONLY | O_CREAT | O_EXCL, 0600);
}

int nbt_fs_setup_layout(nbt_fs *fs) {
    char path[NBT_PATH_MAX];
    const size_t count = sizeof(nbt::storage::kSetupDirs) / sizeof(nbt::storage::kSetupDirs[0]);
    if (!fs || nbt::storage::SafePathOpen::privateDir(fs, fs->root, true)) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (nbt_fs_path(fs, nbt::storage::kSetupDirs[i], "manager", NULL, path, sizeof(path))) {
            if (errno == ENOENT) {
                continue;
            }
            return -1;
        }
        if (nbt::storage::SafePathOpen::privateDir(fs, path, false) && errno != ENOENT) {
            return -1;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (nbt_fs_path(fs, nbt::storage::kSetupDirs[i], "manager", NULL, path, sizeof(path))) {
            return -1;
        }
        if (nbt::storage::SafePathOpen::privateDir(fs, path, false)) {
            if (errno != ENOENT || mkdir(path, 0700) ||
                nbt::storage::SafePathOpen::privateDir(fs, path, false)) {
                return -1;
            }
        }
    }
    return nbt_fs_setup_sync(fs);
}

int nbt_fs_setup_publish(nbt_fs *fs, nbt_fs_path_kind next) {
    char src[NBT_PATH_MAX];
    char dst[NBT_PATH_MAX];
    nbt_fs_path_kind target;
    if (next == NBT_FS_SETUP_KEY_NEXT) {
        target = NBT_FS_MANAGER_KEY;
    } else if (next == NBT_FS_SETUP_PROFILE_NEXT) {
        target = NBT_FS_MANAGER_PROFILE;
    } else if (next == NBT_FS_SETUP_LAUNCHER_NEXT) {
        target = NBT_FS_SETUP_LAUNCHER;
    } else {
        errno = EPERM;
        return -1;
    }
    if (nbt_fs_path(fs, next, "manager", NULL, src, sizeof(src)) ||
        nbt_fs_path(fs, target, "manager", NULL, dst, sizeof(dst)) ||
        nbt::storage::SafePathOpen::safeExisting(fs, src)) {
        return -1;
    }
    if (!nbt::storage::SafePathOpen::safeExisting(fs, dst)) {
        errno = EEXIST;
        return -1;
    }
    if (errno != ENOENT) {
        return -1;
    }
    return rename(src, dst);
}

int nbt_fs_setup_discard(nbt_fs *fs, nbt_fs_path_kind next) {
    char path[NBT_PATH_MAX];
    if (!nbt::storage::setupNextKind(next)) {
        errno = EPERM;
        return -1;
    }
    if (nbt_fs_path(fs, next, "manager", NULL, path, sizeof(path)) ||
        nbt::storage::SafePathOpen::safeExisting(fs, path)) {
        return -1;
    }
    return unlink(path);
}

int nbt_fs_setup_sync(nbt_fs *fs) {
    const int fd = nbt_fs_open(fs, NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0) {
        return -1;
    }
    if (fsync(fd)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    if (close(fd)) {
        return -1;
    }
#ifndef __QNX__
    const size_t count = sizeof(nbt::storage::kSetupDirs) / sizeof(nbt::storage::kSetupDirs[0]);
    if (nbt::storage::SafePathOpen::syncDirKinds(fs, "manager", NULL, nbt::storage::kSetupDirs,
                                                 count) ||
        nbt::storage::SafePathOpen::syncDirPath(fs->root)) {
        return -1;
    }
#endif
    return 0;
}
