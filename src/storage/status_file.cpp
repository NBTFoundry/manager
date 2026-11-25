// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/status_file.hpp"

#include "nbt/bridge.h"
#include "nbt/log.h"
#include "nbt/storage/path_open.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

bool statusRegular(const struct stat *st) {
#ifdef __QNXNTO__
    // Every /dev/shmem object reports 2 or 3 links on the car (inventory
    // ls-shmem.txt, NBT-AAD's files too; dev32 refused ours with EPERM), and
    // nothing on another filesystem can be hard-linked into it.
    const bool oneName = true;
#else
    const bool oneName = st->st_nlink == 1;
#endif
    return S_ISREG(st->st_mode) && st->st_uid == geteuid() && oneName && !(st->st_mode & 0077);
}

// Never seen on the car's /dev/shmem: say which check refused the file.
void refused(const char *path, const struct stat &st) {
    nbt_logf("status file %s: mode %lo, %lu links, owner %ld (want a 0600 regular file, 1 link, "
             "owner %ld)",
             path, static_cast<unsigned long>(st.st_mode), static_cast<unsigned long>(st.st_nlink),
             static_cast<long>(st.st_uid), static_cast<long>(geteuid()));
}

} // namespace

core::Result<core::UniqueFd> StatusFile::open(const ManagedRoot &root, const char *app,
                                              bool create) {
    if (!root.valid()) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::error(EINVAL, "managed root is not initialized"));
    }
    const int fd = nbt_fs_status_open(const_cast<nbt_fs *>(root.legacy()), app, create ? 1 : 0);
    if (fd < 0) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("status file open failed"));
    }
    return core::Result<core::UniqueFd>::success(core::UniqueFd(fd));
}

core::Status StatusFile::write(int fd, core::Span<const char> line) {
    if (nbt_fs_status_write(fd, line.data(), line.size())) {
        return core::Status::fromErrno("status file write failed");
    }
    return core::Status();
}

} // namespace storage
} // namespace nbt

int nbt_fs_status_open(nbt_fs *fs, const char *app, int create) {
    return nbt_fs_bridge_open(fs, app, "status", create);
}

const char *nbt_fs_bridge_dir(const nbt_fs *fs) {
#ifdef NBT_TESTING
    return fs ? fs->root : NULL;
#else
    (void)fs;
    return "/dev/shmem";
#endif
}

int nbt_fs_bridge_open(nbt_fs *fs, const char *app, const char *suffix, int create) {
    char path[NBT_PATH_MAX];
    struct stat before;
    struct stat after;
    int flags = (create ? O_RDWR | O_CREAT | O_EXCL : O_RDONLY) | O_NONBLOCK;
    if (!fs || !nbt::storage::SafePathOpen::validApp(app) || !strcmp(app, "manager") ||
        (create != 0 && create != 1) ||
        nbt::storage::SafePathOpen::privateDir(fs, fs->root, true)) {
        errno = EINVAL;
        return -1;
    }
    if (!suffix || (strcmp(suffix, "status") && strcmp(suffix, "hold") && strcmp(suffix, "page"))) {
        errno = EINVAL;
        return -1;
    }
    const int n =
        snprintf(path, sizeof(path), "%s/nbt-bridge1-%s.%s", nbt_fs_bridge_dir(fs), app, suffix);
    if (n < 0 || n >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    const bool exists = !lstat(path, &before);
    if (!exists && errno != ENOENT) {
        return -1;
    }
    if (exists && !nbt::storage::statusRegular(&before)) {
        nbt::storage::refused(path, before);
        errno = EPERM;
        return -1;
    }
    if (!create && !exists) {
        errno = ENOENT;
        return -1;
    }
    if (create && exists && unlink(path)) {
        return -1;
    }
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path, flags, 0600);
    if (fd < 0) {
        return -1;
    }
    const bool stated = !fstat(fd, &after);
    if (stated && !nbt::storage::statusRegular(&after)) {
        nbt::storage::refused(path, after);
    }
    if (!stated || !nbt::storage::statusRegular(&after) ||
        (!create && (before.st_dev != after.st_dev || before.st_ino != after.st_ino)) ||
        fcntl(fd, F_SETFD, FD_CLOEXEC)) {
        const int saved = errno;
        close(fd);
        errno = saved ? saved : EPERM;
        return -1;
    }
    return fd;
}

int nbt_fs_status_write(int fd, const char *line, size_t length) {
    struct stat st;
    ssize_t n = 0;
    if (!line || !length || length > NBT_BRIDGE_STATUS_LINE_MAX || fstat(fd, &st) ||
        !nbt::storage::statusRegular(&st)) {
        errno = EINVAL;
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    do {
        n = write(fd, line, length);
    } while (n < 0 && errno == EINTR);
    if (n != (ssize_t)length) {
        return -1;
    }
    return ftruncate(fd, (off_t)length);
}
