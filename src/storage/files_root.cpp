// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/files_root.hpp"

#include "nbt/storage/files_listing.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/storage/remove_tree.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

const char kLocation[] = "/mnt/share/nbt-manager";

bool fits(int n, core::Span<char> out) {
    return n >= 0 && static_cast<size_t>(n) < out.size();
}

/* "<a>/<b>", or "<a>/<b>/<c>" when `c`. */
bool join(core::Span<char> out, const char *a, const char *b, const char *c = NULL) {
    return c ? fits(snprintf(out.data(), out.size(), "%s/%s/%s", a, b, c), out)
             : fits(snprintf(out.data(), out.size(), "%s/%s", a, b), out);
}

core::Status refuse(int code, const char *message) {
    errno = code;
    return core::Status::error(code, message);
}

} // namespace

FilesRoot::FilesRoot() : fs_(), valid_(false) {
}

const char *FilesRoot::location() {
#ifdef NBT_TESTING
    const char *path = getenv("NBT_TEST_FILES_ROOT");
    if (path && path[0]) {
        return path;
    }
#endif
    return kLocation;
}

core::Status FilesRoot::open(const char *path, bool create) {
    struct stat st;
    valid_ = false;
    if (!path || path[0] != '/' || strlen(path) >= sizeof(fs_.root)) {
        return refuse(EINVAL, "invalid files root");
    }
    if (lstat(path, &st)) {
        if (errno != ENOENT || !create) {
            return core::Status::fromErrno("no files root");
        }
        if (mkdir(path, 0700) && errno != EEXIST) {
            return core::Status::fromErrno("cannot create the files root");
        }
    }
    // Owned, private, a real directory on a writable volume.
    struct statvfs volume;
    errno = 0;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) ||
        statvfs(path, &volume) || (volume.f_flag & ST_RDONLY)) {
        return refuse(errno ? errno : EPERM, "files root is unsafe");
    }
    memcpy(fs_.root, path, strlen(path) + 1);
    fs_.device = st.st_dev;
    fs_.inode = st.st_ino;
    fs_.owner = st.st_uid;
    if (SafePathOpen::privateDir(&fs_, fs_.root, true) || !folder("files", create).ok() ||
        !folder("staging", create).ok()) {
        return core::Status::fromErrno("files root is unsafe");
    }
    valid_ = true;
    return core::Status();
}

core::Status FilesRoot::folder(const char *name, bool create) const {
    char path[NBT_PATH_MAX];
    if (!join(core::Span<char>(path, sizeof(path)), fs_.root, name)) {
        return refuse(ENAMETOOLONG, "files path too long");
    }
    if (SafePathOpen::privateDir(&fs_, fs_.root, true)) {
        return core::Status::fromErrno("files root changed");
    }
    if (create && mkdir(path, 0700) && errno != EEXIST) {
        return core::Status::fromErrno("cannot create a files folder");
    }
    if (SafePathOpen::privateDir(&fs_, path, false)) {
        return core::Status::fromErrno("files folder is unsafe");
    }
    return core::Status();
}

core::Status FilesRoot::appDir(const char *app, bool create, core::Span<char> out) const {
    if (!valid_ || !SafePathOpen::validApp(app) || !strcmp(app, "manager")) {
        return refuse(EINVAL, "invalid application");
    }
    if (!folder("files", false).ok() || !join(out, fs_.root, "files", app)) {
        return refuse(errno ? errno : ENAMETOOLONG, "files folder unavailable");
    }
    if (create && mkdir(out.data(), 0700) && errno != EEXIST) {
        return core::Status::fromErrno("cannot create the application's files folder");
    }
    if (SafePathOpen::privateDir(&fs_, out.data(), false)) {
        return core::Status::fromErrno("application's files folder unavailable");
    }
    return core::Status();
}

core::Status FilesRoot::filePath(const char *app, const char *name, core::Span<char> out) const {
    char dir[NBT_PATH_MAX];
    if (!nbt_files_name_valid(name)) {
        return refuse(EINVAL, "invalid file name");
    }
    const core::Status status = appDir(app, true, core::Span<char>(dir, sizeof(dir)));
    if (!status.ok()) {
        return status;
    }
    return join(out, dir, name) ? core::Status() : refuse(ENAMETOOLONG, "files path too long");
}

core::Status FilesRoot::stagingPath(const char *app, FilesSource source,
                                    core::Span<char> out) const {
    if (!valid_ || !SafePathOpen::validApp(app) || !strcmp(app, "manager") ||
        !folder("staging", false).ok() ||
        !fits(snprintf(out.data(), out.size(), "%s/staging/%s.%s", fs_.root, app,
                       source == FilesSource::Phone ? "phone" : "usb"),
              out)) {
        return refuse(errno ? errno : EINVAL, "staging unavailable");
    }
    return core::Status();
}

core::Result<FilesUsage> FilesRoot::list(const char *app, const char *after, FileEntry *entries,
                                         size_t capacity, size_t *count) const {
    char dir[NBT_PATH_MAX];
    *count = 0;
    const core::Status status = appDir(app, true, core::Span<char>(dir, sizeof(dir)));
    if (!status.ok()) {
        return core::Result<FilesUsage>::failure(status);
    }
    return listFolder(dir, fs_.device, fs_.owner, after, entries, capacity, count);
}

core::Result<uint64_t> FilesRoot::freeBytes() const {
    struct statvfs volume;
    if (!valid_ || statvfs(fs_.root, &volume) || (volume.f_flag & ST_RDONLY)) {
        return core::Result<uint64_t>::failure(core::Status::fromErrno("disk unavailable"));
    }
    return core::Result<uint64_t>::success(static_cast<uint64_t>(volume.f_bavail) *
                                           static_cast<uint64_t>(volume.f_frsize));
}

core::Status FilesRoot::admit(const char *app, const nbt_files_policy &policy, const char *name,
                              uint64_t size) const {
    if (!nbt_files_policy_accepts(&policy, name)) {
        return refuse(EINVAL, "file type not accepted");
    }
    if (size > nbt_files_policy_file_bytes(&policy)) {
        return refuse(EFBIG, "file too large");
    }
    size_t found = 0;
    core::Result<FilesUsage> usage = list(app, NULL, NULL, 0, &found);
    if (!usage.ok()) {
        return usage.status();
    }
    char path[NBT_PATH_MAX];
    struct stat st;
    uint64_t replaced = 0;
    bool exists = false;
    if (filePath(app, name, core::Span<char>(path, sizeof(path))).ok() && !lstat(path, &st)) {
        if (SafePathOpen::checkOwnedFile(fs_.device, fs_.owner, path)) {
            return refuse(EEXIST, "a file of that name cannot be replaced");
        }
        exists = true;
        replaced = static_cast<uint64_t>(st.st_size);
    }
    const FilesUsage &now = usage.value();
    if (!exists && now.count >= NBT_FILES_COUNT_MAX) {
        return refuse(ENOSPC, "too many files");
    }
    const uint64_t kept = now.bytes > replaced ? now.bytes - replaced : 0;
    if (kept + size > nbt_files_policy_quota_bytes(&policy)) {
        return refuse(ENOSPC, "quota exceeded");
    }
    core::Result<uint64_t> available = freeBytes();
    if (!available.ok() || available.value() < kReserveBytes ||
        available.value() - kReserveBytes < size) {
        return refuse(ENOSPC, "disk full");
    }
    return core::Status();
}

core::Result<core::UniqueFd> FilesRoot::stage(const char *app, FilesSource source) const {
    char path[NBT_PATH_MAX];
    core::Status status = stagingPath(app, source, core::Span<char>(path, sizeof(path)));
    if (status.ok() && unlink(path) && errno != ENOENT) {
        status = core::Status::fromErrno("cannot clear the staging file");
    }
    if (!status.ok()) {
        return core::Result<core::UniqueFd>::failure(status);
    }
    const int fd =
        SafePathOpen::openOwnedFile(fs_.device, fs_.owner, path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("cannot create the staging file"));
    }
    return core::Result<core::UniqueFd>::success(core::UniqueFd(fd));
}

core::Status FilesRoot::commit(const char *app, FilesSource source, const char *name) const {
    char from[NBT_PATH_MAX], to[NBT_PATH_MAX];
    struct stat st;
    core::Status status = stagingPath(app, source, core::Span<char>(from, sizeof(from)));
    if (status.ok()) {
        status = filePath(app, name, core::Span<char>(to, sizeof(to)));
    }
    if (!status.ok()) {
        return status;
    }
    if (SafePathOpen::checkOwnedFile(fs_.device, fs_.owner, from)) {
        return core::Status::fromErrno("staging file is unsafe");
    }
    if (!lstat(to, &st) && SafePathOpen::checkOwnedFile(fs_.device, fs_.owner, to)) {
        return refuse(EEXIST, "a file of that name cannot be replaced");
    }
    if (rename(from, to)) {
        return core::Status::fromErrno("cannot store the file");
    }
#ifndef __QNX__
    // The rename itself; on QNX the file's fsync committed the filesystem already.
    char dir[NBT_PATH_MAX];
    if (appDir(app, false, core::Span<char>(dir, sizeof(dir))).ok()) {
        (void)SafePathOpen::syncDirPath(dir);
    }
#endif
    return core::Status();
}

void FilesRoot::discard(const char *app, FilesSource source) const {
    char path[NBT_PATH_MAX];
    if (stagingPath(app, source, core::Span<char>(path, sizeof(path))).ok() &&
        !SafePathOpen::checkOwnedFile(fs_.device, fs_.owner, path)) {
        (void)unlink(path);
    }
}

core::Status FilesRoot::remove(const char *app, const char *name) const {
    char path[NBT_PATH_MAX];
    const size_t length = name ? strlen(name) : 0;
    if (length > 1 && length <= NBT_FILES_FOLDER_MAX + 1 && name[length - 1] == '/') {
        char folder[NBT_FILES_FOLDER_MAX + 1];
        memcpy(folder, name, length - 1);
        folder[length - 1] = 0;
        const core::Status found = folderPath(app, folder, core::Span<char>(path, sizeof(path)));
        return found.ok() ? removeFolder(path) : found;
    }
    const core::Status status = filePath(app, name, core::Span<char>(path, sizeof(path)));
    if (!status.ok()) {
        return status;
    }
    if (SafePathOpen::checkOwnedFile(fs_.device, fs_.owner, path)) {
        return core::Status::fromErrno(errno == ENOENT ? "no such file" : "file is unsafe");
    }
    return unlink(path) ? core::Status::fromErrno("cannot delete the file") : core::Status();
}

core::Status FilesRoot::removeApp(const char *app) const {
    char path[NBT_PATH_MAX];
    discard(app, FilesSource::Phone);
    discard(app, FilesSource::Usb);
    discardFolder(app, FilesSource::Phone);
    discardFolder(app, FilesSource::Usb);
    const core::Status status = appDir(app, false, core::Span<char>(path, sizeof(path)));
    if (!status.ok()) {
        return status.code() == ENOENT ? core::Status() : status;
    }
    if (removeTree(path, strlen(path), fs_.device, 0)) {
        return core::Status::fromErrno("cannot delete the application's files");
    }
    return core::Status();
}

} // namespace storage
} // namespace nbt
