// SPDX-License-Identifier: MPL-2.0
#include "nbt/app_remove.h"

#include "nbt/core/unique_fd.hpp"
#include "nbt/package.h"
#include "nbt/storage/files_root.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

int nbt_app_remove(nbt_fs *fs, const char *app) {
    char path[NBT_PATH_MAX];
    struct stat st;
    if (!fs || !nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager")) {
        errno = EINVAL;
        return -1;
    }
    const int operationFd = nbt_tx_manager_lock(fs);
    if (operationFd == -2) {
        errno = ENOENT;
        return -1;
    }
    nbt::core::UniqueFd operation(operationFd);
    if (!operation.valid()) {
        return NBT_APP_REMOVE_BUSY;
    }
    const int present = nbt_fs_app_present(fs, app);
    if (present <= 0) {
        return present ? -1 : NBT_APP_REMOVE_MISSING;
    }
    if (nbt_fs_path(fs, NBT_FS_STATE_DIR, app, NULL, path, sizeof(path))) {
        return -1;
    }
    // Without a state directory (an earlier removal cut short) no host can run it.
    nbt::core::UniqueFd host;
    nbt::core::UniqueFd tx;
    nbt::core::UniqueFd lease;
    if (!lstat(path, &st)) {
        host.reset(nbt::storage::SafePathOpen::lockNonBlocking(fs, NBT_FS_HOST_LOCK, app));
        if (!host.valid()) {
            return errno == EAGAIN || errno == EACCES ? NBT_APP_REMOVE_RUNNING : -1;
        }
        tx.reset(nbt_tx_lock(fs, app));
        if (!tx.valid()) {
            return NBT_APP_REMOVE_BUSY;
        }
        lease.reset(nbt_fs_lease_exclusive(fs, app));
        if (!lease.valid()) {
            return NBT_APP_REMOVE_RUNNING;
        }
    } else if (errno != ENOENT) {
        return -1;
    }
    // Its files first, still under its locks: a failure leaves the application
    // installed, to remove again. Without the disk they stay (docs/files-bridge.md).
    nbt::storage::FilesRoot files;
    if (files.open(nbt::storage::FilesRoot::location(), false).ok() && !files.removeApp(app).ok()) {
        return -1;
    }
    return nbt_fs_app_remove(fs, app) ? -1 : NBT_APP_REMOVED;
}
