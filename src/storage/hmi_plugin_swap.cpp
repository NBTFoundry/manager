// SPDX-License-Identifier: MPL-2.0
/* Replacing the live iDrive plugin. iDrive maps libNbtManager.so while it
 * runs, so no live file is ever rewritten: the new set is written and checked
 * in `.NbtManager.setup`, then `NbtManager` becomes `.NbtManager.old` and the
 * setup folder becomes `NbtManager`. Interrupted between the two renames, the
 * car has no NBT Manager plugin (iDrive itself is untouched) until the next
 * refresh installs one as a first registration and drops the old folder. */
#include "nbt/core/fault.hpp"
#include "nbt/fs.h"
#include "nbt/storage/path_open.hpp"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using nbt::storage::SafePathOpen;

struct Paths {
    char plugins[NBT_PATH_MAX];
    char live[NBT_PATH_MAX];
    char temp[NBT_PATH_MAX];
    char old[NBT_PATH_MAX];
};

int ownedDir(nbt_hmi_fs *h, const char *path, bool isRoot) {
    return SafePathOpen::checkOwnedDir(h->device, h->owner, h->inode, path, isRoot, false);
}

int paths(nbt_hmi_fs *h, Paths &p) {
    if (!h || ownedDir(h, h->root, true) ||
        snprintf(p.plugins, sizeof(p.plugins), "%s/Plugins", h->root) >= NBT_PATH_MAX ||
        snprintf(p.live, sizeof(p.live), "%s/NbtManager", p.plugins) >= NBT_PATH_MAX ||
        snprintf(p.temp, sizeof(p.temp), "%s/.NbtManager.setup", p.plugins) >= NBT_PATH_MAX ||
        snprintf(p.old, sizeof(p.old), "%s/.NbtManager.old", p.plugins) >= NBT_PATH_MAX ||
        ownedDir(h, p.plugins, false)) {
        errno = errno ? errno : EPERM;
        return -1;
    }
    return 0;
}

/* 1 when `path` is a safe folder of ours, 0 when absent, -1 otherwise. */
int present(nbt_hmi_fs *h, const char *path) {
    if (!ownedDir(h, path, false)) {
        return 1;
    }
    return errno == ENOENT ? 0 : -1;
}

} // namespace

int nbt_hmi_prepare_replace(nbt_hmi_fs *h) {
    Paths p;
    if (paths(h, p) || nbt_hmi_entries(h, 0) != 1 || nbt_hmi_entries(h, 1) < 0 ||
        present(h, p.old) != 0) {
        return -1;
    }
    const int temp = present(h, p.temp);
    if (temp < 0 || (!temp && (mkdir(p.temp, 0700) || ownedDir(h, p.temp, false)))) {
        return -1;
    }
    return 0;
}

int nbt_hmi_replace(nbt_hmi_fs *h) {
    Paths p;
    if (paths(h, p) || nbt_hmi_entries(h, 0) != 1 || nbt_hmi_entries(h, 1) != 1 ||
        present(h, p.old) != 0 || chmod(p.temp, 0755) || nbt_hmi_sync(h, 1)) {
        return -1;
    }
    NBT_FAULT_POINT("NBT_HMI_FS_FAULT", "replace-synced");
    if (rename(p.live, p.old)) {
        return -1;
    }
    NBT_FAULT_POINT("NBT_HMI_FS_FAULT", "replace-old");
    if (rename(p.temp, p.live)) {
        return -1;
    }
    NBT_FAULT_POINT("NBT_HMI_FS_FAULT", "replace-live");
    if (nbt_hmi_sync(h, 0)) {
        return -1;
    }
    return nbt_hmi_drop_old(h);
}

/* Only the five plugin names are removed; anything else keeps the folder,
 * and rmdir then fails rather than deleting what is not ours. */
int nbt_hmi_drop_old(nbt_hmi_fs *h) {
    Paths p;
    if (paths(h, p)) {
        return -1;
    }
    const int old = present(h, p.old);
    if (old <= 0) {
        return old;
    }
    for (unsigned i = 0; i < NBT_HMI_FILES; ++i) {
        char file[NBT_PATH_MAX];
        if (snprintf(file, sizeof(file), "%s/%s", p.old, nbt_hmi_name(i)) >= NBT_PATH_MAX) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (SafePathOpen::checkOwnedFile(h->device, h->owner, file)) {
            if (errno == ENOENT) {
                continue;
            }
            return -1;
        }
        if (unlink(file)) {
            return -1;
        }
    }
    if (rmdir(p.old)) {
        return -1;
    }
#ifndef __QNX__
    return SafePathOpen::syncDirPath(p.plugins);
#else
    return 0;
#endif
}
