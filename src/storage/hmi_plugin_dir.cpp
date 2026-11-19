#include "nbt/storage/hmi_plugin_dir.hpp"

#include "nbt/core/fault.hpp"
#include "nbt/storage/path_open.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

using nbt::storage::SafePathOpen;

const char *const kHmiNames[NBT_HMI_FILES] = {"libNbtManager.so", "NbtManager.manifest",
                                              "NbtManager.xml", "NbtManager_ALL_Texts.zip",
                                              "NbtManager_ALL_Images.zip"};

int hmiDir(nbt_hmi_fs *h, const char *path, bool isRoot) {
    return SafePathOpen::checkOwnedDir(h->device, h->owner, h->inode, path, isRoot, false);
}

int hmiFile(nbt_hmi_fs *h, const char *path) {
    return SafePathOpen::checkOwnedFile(h->device, h->owner, path);
}

int hmiPaths(nbt_hmi_fs *h, int temporary, unsigned index, char *plugins, char *directory,
             char *file) {
    if (!h || (temporary != 0 && temporary != 1) || index >= NBT_HMI_FILES ||
        hmiDir(h, h->root, true) ||
        snprintf(plugins, NBT_PATH_MAX, "%s/Plugins", h->root) >= NBT_PATH_MAX ||
        snprintf(directory, NBT_PATH_MAX, "%s/%s", plugins,
                 temporary ? ".NbtManager.setup" : "NbtManager") >= NBT_PATH_MAX ||
        snprintf(file, NBT_PATH_MAX, "%s/%s", directory, kHmiNames[index]) >= NBT_PATH_MAX) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int hmiVerifiedPaths(nbt_hmi_fs *h, int temporary, unsigned index, char *plugins, char *directory,
                     char *file) {
    if (hmiPaths(h, temporary, index, plugins, directory, file) || hmiDir(h, plugins, false) ||
        hmiDir(h, directory, false)) {
        return -1;
    }
    return 0;
}

int hmiRefuse(const char **reason, const char *text, int error) {
    if (reason) {
        *reason = text;
    }
    errno = error;
    return -1;
}

int ensureDir(nbt_hmi_fs *h, const char *path, mode_t mode) {
    if (!hmiDir(h, path, false)) {
        return 0;
    }
    return (errno == ENOENT && !mkdir(path, mode) && !hmiDir(h, path, false)) ? 0 : -1;
}

int hmiOpenInternal(nbt_hmi_fs *h, int temporary, unsigned index, int mode) {
    char plugins[NBT_PATH_MAX], directory[NBT_PATH_MAX], file[NBT_PATH_MAX];
    if (hmiVerifiedPaths(h, temporary, index, plugins, directory, file) ||
        (mode == 1 && !temporary) || (mode != 0 && mode != 1 && mode != 2)) {
        errno = EPERM;
        return -1;
    }
    if (mode != 1 && hmiFile(h, file)) {
        return -1;
    }
    const int flags = mode == 1 ? (O_WRONLY | O_CREAT | O_EXCL) : (mode == 2 ? O_WRONLY : O_RDONLY);
    return SafePathOpen::openOwnedFile(h->device, h->owner, file, flags, 0600);
}

void hmiFault(const char *step) {
    NBT_FAULT_POINT("NBT_HMI_FS_FAULT", step);
}

int requireEmptyLivePlugin(nbt_hmi_fs *h) {
    const int status = nbt_hmi_entries(h, 0);
    if (status > 0) {
        errno = EEXIST;
    }
    return status == 0 ? 0 : -1;
}

nbt::core::Status call(int res, const char *msg) {
    return res ? nbt::core::Status::fromErrno(msg) : nbt::core::Status();
}

} // namespace

int nbt_hmi_check(nbt_hmi_fs *h, const char *root, const char **reason) {
    struct stat st;
    struct statvfs volume;
    if (reason) {
        *reason = NULL;
    }
    if (!h || !root || root[0] != '/') {
        return hmiRefuse(reason, "invalid plugin folder path", EINVAL);
    } else if (lstat(root, &st) || !S_ISDIR(st.st_mode)) {
        return hmiRefuse(reason, "plugin folder missing", ENOENT);
    } else if (strlen(root) >= sizeof(h->root)) {
        return hmiRefuse(reason, "plugin folder path too long", ENAMETOOLONG);
    }
#ifndef NBT_TESTING
    if (strcmp(root, "/opt/hmi/ID5/data/ro")) {
        return hmiRefuse(reason, "not the iDrive plugin folder", EPERM);
    }
#endif
    if (st.st_uid != geteuid()) {
        return hmiRefuse(reason, "plugin folder owned by another user", EPERM);
    } else if (!SafePathOpen::carModeSafe(&st)) {
        return hmiRefuse(reason, "plugin folder writable by other users", EPERM);
    } else if (statvfs(root, &volume)) {
        return hmiRefuse(reason, "plugin filesystem cannot be queried", EIO);
    } else if (volume.f_flag & ST_RDONLY) {
        return hmiRefuse(reason, "plugin filesystem is read-only", EROFS);
    }
#ifdef __QNX__
    if (strcmp(volume.f_basetype, "qnx6")) {
        return hmiRefuse(reason, "plugin filesystem is not qnx6", ENOTSUP);
    } else if (volume.f_flag & ST_NOEXEC) {
        return hmiRefuse(reason, "plugin filesystem is mounted noexec", ENOTSUP);
    }
#endif
    memcpy(h->root, root, strlen(root) + 1);
    h->device = st.st_dev;
    h->inode = st.st_ino;
    h->owner = st.st_uid;
    return hmiDir(h, h->root, true)
               ? hmiRefuse(reason, "plugin folder changed while checked", EPERM)
               : 0;
}

const char *nbt_hmi_name(unsigned index) {
    return index < NBT_HMI_FILES ? kHmiNames[index] : NULL;
}

int nbt_hmi_init(nbt_hmi_fs *h, const char *root) {
    return nbt_hmi_check(h, root, NULL);
}

int nbt_hmi_space(nbt_hmi_fs *h, uint64_t required) {
    struct statvfs volume;
    if (!h || hmiDir(h, h->root, true) || statvfs(h->root, &volume) ||
        !SafePathOpen::volumeHasSpace(volume, required) || NBT_TEST_FLAG("NBT_TEST_HMI_NO_SPACE")) {
        errno = ENOSPC;
        return -1;
    }
    return 0;
}

int nbt_hmi_entries(nbt_hmi_fs *h, int temporary) {
    char plugins[NBT_PATH_MAX], directory[NBT_PATH_MAX], file[NBT_PATH_MAX];
    if (hmiPaths(h, temporary, 0, plugins, directory, file)) {
        return -1;
    }
    if (hmiDir(h, plugins, false) || hmiDir(h, directory, false)) {
        return errno == ENOENT ? 0 : -1;
    }
    DIR *dir = opendir(directory);
    if (!dir) {
        return -1;
    }
    errno = 0;
    for (struct dirent *entry = NULL; (entry = readdir(dir)) != NULL; errno = 0) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        unsigned i = 0;
        for (; i < NBT_HMI_FILES && strcmp(entry->d_name, kHmiNames[i]); ++i) {
        }
        if (i == NBT_HMI_FILES || hmiPaths(h, temporary, i, plugins, directory, file) ||
            hmiFile(h, file)) {
            closedir(dir);
            errno = EPERM;
            return -1;
        }
    }
    if (errno) {
        const int saved = errno;
        closedir(dir);
        errno = saved;
        return -1;
    }
    return closedir(dir) ? -1 : 1;
}

int nbt_hmi_prepare(nbt_hmi_fs *h) {
    char plugins[NBT_PATH_MAX], directory[NBT_PATH_MAX], file[NBT_PATH_MAX];
    if (hmiPaths(h, 1, 0, plugins, directory, file) || requireEmptyLivePlugin(h) ||
        nbt_hmi_entries(h, 1) < 0 || ensureDir(h, plugins, 0755) || ensureDir(h, directory, 0700)) {
        return -1;
    }
    return 0;
}

int nbt_hmi_open(nbt_hmi_fs *h, int temporary, unsigned index, int create) {
    return (create == 0 || create == 1) ? hmiOpenInternal(h, temporary, index, create)
                                        : (errno = EPERM, -1);
}

int nbt_hmi_discard(nbt_hmi_fs *h, unsigned index) {
    char plugins[NBT_PATH_MAX], directory[NBT_PATH_MAX], file[NBT_PATH_MAX];
    if (hmiVerifiedPaths(h, 1, index, plugins, directory, file) || hmiFile(h, file)) {
        return -1;
    }
    return unlink(file);
}

int nbt_hmi_sync(nbt_hmi_fs *h, int temporary) {
    char plugins[NBT_PATH_MAX], directory[NBT_PATH_MAX], file[NBT_PATH_MAX];
    if (hmiVerifiedPaths(h, temporary, 0, plugins, directory, file)) {
        return -1;
    }
    const int fd = hmiOpenInternal(h, temporary, 1, 2);
    if (fd < 0 || SafePathOpen::syncWritableFd(fd)) {
        return -1;
    }
#ifndef __QNX__
    if (SafePathOpen::syncDirPath(directory) || SafePathOpen::syncDirPath(plugins) ||
        SafePathOpen::syncDirPath(h->root)) {
        return -1;
    }
#endif
    return 0;
}

int nbt_hmi_publish(nbt_hmi_fs *h) {
    char plugins[NBT_PATH_MAX], temp[NBT_PATH_MAX], finalPath[NBT_PATH_MAX], file[NBT_PATH_MAX];
    if (nbt_hmi_entries(h, 1) != 1 || hmiPaths(h, 1, 0, plugins, temp, file) ||
        requireEmptyLivePlugin(h) ||
        snprintf(finalPath, sizeof(finalPath), "%s/NbtManager", plugins) >=
            (int)sizeof(finalPath) ||
        chmod(temp, 0755)) {
        return -1;
    }
    hmiFault("chmod");
    if (nbt_hmi_sync(h, 1)) {
        return -1;
    }
    hmiFault("synced");
    if (rename(temp, finalPath)) {
        return -1;
    }
    hmiFault("renamed");
    return nbt_hmi_sync(h, 0);
}

namespace nbt {
namespace storage {

core::Status HmiPluginDir::initialize(const char *root) {
    return call(nbt_hmi_init(&root_, root), "HMI plugin directory is invalid");
}

core::Status HmiPluginDir::requireSpace(uint64_t bytes) const {
    return call(nbt_hmi_space(const_cast<nbt_hmi_fs *>(&root_), bytes), "HMI space check failed");
}

core::Result<core::UniqueFd> HmiPluginDir::open(bool temporary, unsigned index, bool create) const {
    const int fd =
        nbt_hmi_open(const_cast<nbt_hmi_fs *>(&root_), temporary ? 1 : 0, index, create ? 1 : 0);
    if (fd < 0) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("HMI plugin file open failed"));
    }
    return core::Result<core::UniqueFd>::success(core::UniqueFd(fd));
}

core::Status HmiPluginDir::prepare() {
    return call(nbt_hmi_prepare(&root_), "HMI plugin preparation failed");
}

core::Status HmiPluginDir::discard(unsigned index) {
    return call(nbt_hmi_discard(&root_, index), "HMI plugin cleanup failed");
}

core::Status HmiPluginDir::publish() {
    return call(nbt_hmi_publish(&root_), "HMI plugin publish failed");
}

core::Status HmiPluginDir::sync(bool temporary) {
    return call(nbt_hmi_sync(&root_, temporary ? 1 : 0), "HMI plugin sync failed");
}

} // namespace storage
} // namespace nbt
