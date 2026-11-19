#include "nbt/storage/setup.hpp"

#include "nbt/storage/path_open.hpp"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

const nbt_fs_path_kind kProvisionDirs[] = {NBT_FS_DATA_DIR, NBT_FS_PROVISION_APP_ROOT,
                                           NBT_FS_APP_DIR, NBT_FS_STATE_DIR};

const nbt_fs_path_kind kProvisionFiles[] = {NBT_FS_PROFILE,
                                            NBT_FS_KEY,
                                            NBT_FS_RECORD,
                                            NBT_FS_ACTIVE,
                                            NBT_FS_LOCK,
                                            NBT_FS_LEASE,
                                            NBT_FS_SYNC,
                                            NBT_FS_HOST_LOCK,
                                            NBT_FS_SETTINGS_SCHEMA,
                                            NBT_FS_SETTINGS_SCHEMA_NEXT,
                                            NBT_FS_FAULT_GUARD,
                                            NBT_FS_FAULT_GUARD_NEXT,
                                            NBT_FS_DATA_SETTINGS};

core::Status call(int result, const char *message) {
    return result ? core::Status::fromErrno(message) : core::Status();
}

} // namespace

core::Status AppProvisioningDirs::create(const ManagedRoot &root, const char *app) {
    return call(nbt_fs_provision_dirs(const_cast<nbt_fs *>(root.legacy()), app),
                "application provisioning failed");
}

core::Result<core::UniqueFd> AppProvisioningDirs::createFile(const ManagedRoot &root, PathKind kind,
                                                             const char *app, const char *tx) {
    const int fd = nbt_fs_provision_create(const_cast<nbt_fs *>(root.legacy()), kind, app, tx);
    if (fd < 0) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("provisioning file creation failed"));
    }
    return core::Result<core::UniqueFd>::success(core::UniqueFd(fd));
}

core::Status AppProvisioningDirs::discard(const ManagedRoot &root, const char *app) {
    return call(nbt_fs_provision_discard(const_cast<nbt_fs *>(root.legacy()), app),
                "provisioning cleanup failed");
}

core::Status AppProvisioningDirs::sync(const ManagedRoot &root, const char *app) {
    return call(nbt_fs_provision_sync(const_cast<nbt_fs *>(root.legacy()), app),
                "provisioning sync failed");
}

} // namespace storage
} // namespace nbt

int nbt_fs_provision_dirs(nbt_fs *fs, const char *app) {
    char path[NBT_PATH_MAX];
    const size_t count =
        sizeof(nbt::storage::kProvisionDirs) / sizeof(nbt::storage::kProvisionDirs[0]);
    if (!fs || !nbt::storage::SafePathOpen::validApp(app) || !strcmp(app, "manager")) {
        errno = EPERM;
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (nbt_fs_path(fs, nbt::storage::kProvisionDirs[i], app, NULL, path, sizeof(path)) ||
            mkdir(path, 0700) || nbt::storage::SafePathOpen::privateDir(fs, path, false)) {
            return -1;
        }
    }
    return 0;
}

int nbt_fs_provision_create(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx) {
    char path[NBT_PATH_MAX];
    if (!fs || !app || !strcmp(app, "manager") ||
        !(kind == NBT_FS_ACTIVE || kind == NBT_FS_RECORD || kind == NBT_FS_PROFILE ||
          (kind == NBT_FS_KEY && tx && !strcmp(tx, "0")))) {
        errno = EPERM;
        return -1;
    }
    if (nbt_fs_path(fs, kind, app, tx, path, sizeof(path))) {
        return -1;
    }
    return nbt::storage::SafePathOpen::openChecked(fs, path, O_WRONLY | O_CREAT | O_EXCL,
                                                   kind == NBT_FS_ACTIVE ? 0700 : 0600);
}

int nbt_fs_provision_discard(nbt_fs *fs, const char *app) {
    char path[NBT_PATH_MAX];
    int result = 0;
    const size_t fileCount =
        sizeof(nbt::storage::kProvisionFiles) / sizeof(nbt::storage::kProvisionFiles[0]);
    if (!fs || !nbt::storage::SafePathOpen::validApp(app) || !strcmp(app, "manager")) {
        errno = EPERM;
        return -1;
    }
    for (size_t i = 0; i < fileCount; ++i) {
        const nbt_fs_path_kind kind = nbt::storage::kProvisionFiles[i];
        if (nbt_fs_path(fs, kind, app, kind == NBT_FS_KEY ? "0" : NULL, path, sizeof(path))) {
            if (errno != ENOENT) {
                result = -1;
            }
            continue;
        }
        if (!nbt::storage::SafePathOpen::safeExisting(fs, path)) {
            if (unlink(path)) {
                result = -1;
            }
        } else if (errno != ENOENT) {
            result = -1;
        }
    }
    static const nbt_fs_path_kind kDirsReverse[] = {NBT_FS_APP_DIR, NBT_FS_PROVISION_APP_ROOT,
                                                    NBT_FS_STATE_DIR, NBT_FS_DATA_DIR};
    for (size_t i = 0; i < sizeof(kDirsReverse) / sizeof(kDirsReverse[0]); ++i) {
        if (nbt_fs_path(fs, kDirsReverse[i], app, NULL, path, sizeof(path))) {
            if (errno != ENOENT) {
                result = -1;
            }
            continue;
        }
        if (!nbt::storage::SafePathOpen::privateDir(fs, path, false)) {
            if (rmdir(path)) {
                result = -1;
            }
        } else if (errno != ENOENT) {
            result = -1;
        }
    }
    return result;
}

int nbt_fs_provision_sync(nbt_fs *fs, const char *app) {
    if (nbt_fs_barrier(fs, app, NULL)) {
        return -1;
    }
#ifndef __QNX__
    const nbt_fs_path_kind dirs[] = {
        NBT_FS_DATA_DIR,          NBT_FS_SETUP_DATA_DIR,         NBT_FS_SETUP_APPS_DIR,
        NBT_FS_SETUP_STATE_DIR,   NBT_FS_SETUP_APP_PROFILES_DIR, NBT_FS_SETUP_APP_KEYS_DIR,
        NBT_FS_PROVISION_APP_ROOT};
    if (nbt::storage::SafePathOpen::syncDirKinds(fs, app, NULL, dirs,
                                                 sizeof(dirs) / sizeof(dirs[0]))) {
        return -1;
    }
#endif
    return 0;
}
