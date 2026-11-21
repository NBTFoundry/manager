// SPDX-License-Identifier: MPL-2.0
/* Game folders in an application's files folder: one level, imported whole
 * (a folder or an archive from a USB stick, docs/files-bridge.md). */
#include "nbt/storage/files_root.hpp"

#include "nbt/storage/files_listing.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/storage/remove_tree.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

bool fits(int n, core::Span<char> out) {
    return n >= 0 && static_cast<size_t>(n) < out.size();
}

bool join(core::Span<char> out, const char *a, const char *b) {
    return fits(snprintf(out.data(), out.size(), "%s/%s", a, b), out);
}

core::Status refuse(int code, const char *message) {
    errno = code;
    return core::Status::error(code, message);
}

} // namespace

core::Status FilesRoot::folderPath(const char *app, const char *folder,
                                   core::Span<char> out) const {
    char dir[NBT_PATH_MAX];
    if (!nbt_files_folder_valid(folder)) {
        return refuse(EINVAL, "invalid file name");
    }
    const core::Status status = appDir(app, true, core::Span<char>(dir, sizeof(dir)));
    if (!status.ok()) {
        return status;
    }
    return join(out, dir, folder) ? core::Status() : refuse(ENAMETOOLONG, "files path too long");
}

core::Status FilesRoot::removeFolder(const char *path) const {
    struct stat st;
    if (lstat(path, &st)) {
        return core::Status::fromErrno(errno == ENOENT ? "no such file" : "folder is unsafe");
    }
    if (!S_ISDIR(st.st_mode) || st.st_dev != fs_.device || st.st_uid != fs_.owner) {
        return refuse(EPERM, "folder is unsafe");
    }
    char tree[NBT_PATH_MAX];
    const size_t length = strlen(path);
    if (length >= sizeof(tree)) {
        return refuse(ENAMETOOLONG, "files path too long");
    }
    memcpy(tree, path, length + 1);
    return removeTree(tree, length, fs_.device, 0)
               ? core::Status::fromErrno("cannot delete the folder")
               : core::Status();
}

core::Status FilesRoot::admitFolder(const char *app, const nbt_files_policy &policy,
                                    const char *folder, unsigned files, uint64_t bytes) const {
    char path[NBT_PATH_MAX];
    core::Status status = folderPath(app, folder, core::Span<char>(path, sizeof(path)));
    if (!status.ok()) {
        return status;
    }
    size_t found = 0;
    core::Result<FilesUsage> usage = list(app, NULL, NULL, 0, &found);
    if (!usage.ok()) {
        return usage.status();
    }
    FilesUsage replaced = {0, 0};
    struct stat st;
    if (!lstat(path, &st)) {
        size_t none = 0;
        core::Result<FilesUsage> inside =
            listFolder(path, fs_.device, fs_.owner, NULL, NULL, 0, &none);
        if (!S_ISDIR(st.st_mode) || !inside.ok()) {
            return refuse(EEXIST, "a file of that name cannot be replaced");
        }
        replaced = inside.value();
    }
    const FilesUsage &now = usage.value();
    const unsigned keptFiles = now.count > replaced.count ? now.count - replaced.count : 0;
    if (!files || keptFiles + files > NBT_FILES_COUNT_MAX) {
        return refuse(ENOSPC, "too many files");
    }
    const uint64_t kept = now.bytes > replaced.bytes ? now.bytes - replaced.bytes : 0;
    if (kept + bytes > nbt_files_policy_quota_bytes(&policy)) {
        return refuse(ENOSPC, "quota exceeded");
    }
    core::Result<uint64_t> available = freeBytes();
    if (!available.ok() || available.value() < kReserveBytes ||
        available.value() - kReserveBytes < bytes) {
        return refuse(ENOSPC, "disk full");
    }
    return core::Status();
}

core::Status FilesRoot::stageFolder(const char *app, FilesSource source,
                                    core::Span<char> out) const {
    char file[NBT_PATH_MAX];
    core::Status status = stagingPath(app, source, core::Span<char>(file, sizeof(file)));
    if (!status.ok() || !fits(snprintf(out.data(), out.size(), "%s.d", file), out)) {
        return status.ok() ? refuse(ENAMETOOLONG, "staging unavailable") : status;
    }
    struct stat st;
    if (!lstat(out.data(), &st)) {
        status = removeFolder(out.data());
        if (!status.ok()) {
            return status;
        }
    }
    if (mkdir(out.data(), 0700) || SafePathOpen::privateDir(&fs_, out.data(), false)) {
        return core::Status::fromErrno("cannot create the staging folder");
    }
    return core::Status();
}

core::Result<core::UniqueFd> FilesRoot::stageFolderFile(const char *staged,
                                                        const char *name) const {
    char path[NBT_PATH_MAX];
    if (!nbt_files_name_valid(name) || !join(core::Span<char>(path, sizeof(path)), staged, name)) {
        return core::Result<core::UniqueFd>::failure(refuse(EINVAL, "invalid file name"));
    }
    const int fd =
        SafePathOpen::openOwnedFile(fs_.device, fs_.owner, path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("cannot create the staging file"));
    }
    return core::Result<core::UniqueFd>::success(core::UniqueFd(fd));
}

core::Status FilesRoot::commitFolder(const char *app, FilesSource source,
                                     const char *folder) const {
    char staged[NBT_PATH_MAX], file[NBT_PATH_MAX], to[NBT_PATH_MAX];
    core::Status status = stagingPath(app, source, core::Span<char>(file, sizeof(file)));
    if (status.ok() && !fits(snprintf(staged, sizeof(staged), "%s.d", file),
                             core::Span<char>(staged, sizeof(staged)))) {
        status = refuse(ENAMETOOLONG, "staging unavailable");
    }
    if (status.ok()) {
        status = folderPath(app, folder, core::Span<char>(to, sizeof(to)));
    }
    if (!status.ok()) {
        return status;
    }
    if (SafePathOpen::privateDir(&fs_, staged, false)) {
        return core::Status::fromErrno("staging folder is unsafe");
    }
    struct stat st;
    if (!lstat(to, &st)) {
        status = removeFolder(to);
        if (!status.ok()) {
            return status;
        }
    }
    if (rename(staged, to)) {
        return core::Status::fromErrno("cannot store the folder");
    }
#ifndef __QNX__
    char dir[NBT_PATH_MAX];
    if (appDir(app, false, core::Span<char>(dir, sizeof(dir))).ok()) {
        (void)SafePathOpen::syncDirPath(dir);
    }
#endif
    return core::Status();
}

void FilesRoot::discardFolder(const char *app, FilesSource source) const {
    char file[NBT_PATH_MAX], staged[NBT_PATH_MAX];
    struct stat st;
    if (stagingPath(app, source, core::Span<char>(file, sizeof(file))).ok() &&
        fits(snprintf(staged, sizeof(staged), "%s.d", file),
             core::Span<char>(staged, sizeof(staged))) &&
        !lstat(staged, &st)) {
        (void)removeFolder(staged);
    }
}

} // namespace storage
} // namespace nbt
