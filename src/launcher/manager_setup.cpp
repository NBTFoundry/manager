// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/manager_setup.hpp"

#include "nbt/launcher/slot_fs.hpp"
#include "nbt/manager_setup.h"
#include "nbt/manager_stage.h"
#include "nbt/manager_store.h"
#include "nbt/release.h"
#include "nbt/util.h"

#include <errno.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using nbt::launcher::SlotFs;

const unsigned kKeyMax = 16384u;

void fault(const char *step) {
#ifdef NBT_TESTING
    const char *name = getenv("NBT_MANAGER_SETUP_FAULT");
    if (name && !strcmp(name, step)) {
        _exit(91);
    }
#else
    (void)step;
#endif
}

void checkpoint(const char *step, const char *phase) {
#ifdef NBT_TESTING
    char name[64];
    if (snprintf(name, sizeof(name), "%s-%s", step, phase) > 0) {
        fault(name);
    }
#else
    (void)step;
    (void)phase;
#endif
}

int readKey(int fd, unsigned char key[kKeyMax], size_t *length) {
    struct stat st;
    size_t used = 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        static_cast<uint64_t>(st.st_size) > kKeyMax || lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    while (used < static_cast<size_t>(st.st_size)) {
        const ssize_t n = read(fd, key + used, static_cast<size_t>(st.st_size) - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        used += static_cast<size_t>(n);
    }
    *length = used;
    return 0;
}

int prepareNext(nbt_fs *fs, nbt_fs_path_kind next) {
    const int exists = nbt_fs_exists(fs, next, "manager", NULL);
    if (exists < 0) {
        return -1;
    }
    if (exists && (nbt_fs_setup_discard(fs, next) || nbt_fs_setup_sync(fs))) {
        return -1;
    }
    return nbt_fs_setup_create(fs, next);
}

int publishBytes(nbt_fs *fs, nbt_fs_path_kind target, nbt_fs_path_kind next, const void *bytes,
                 size_t length, const char *step) {
    const int exists = nbt_fs_exists(fs, target, "manager", NULL);
    if (exists != 0) {
        return exists > 0 ? 0 : -1;
    }
    const int fd = prepareNext(fs, next);
    if (fd < 0) {
        return -1;
    }
    checkpoint(step, "created");
    if (SlotFs::writeAll(fd, bytes, length)) {
        close(fd);
        return -1;
    }
    checkpoint(step, "written");
    if (fsync(fd)) {
        close(fd);
        return -1;
    }
    checkpoint(step, "synced");
    if (close(fd) || nbt_fs_setup_publish(fs, next) || nbt_fs_setup_sync(fs)) {
        return -1;
    }
    fault(step);
    return 0;
}

int publishLauncher(nbt_fs *fs, int releaseFd, const nbt_release *release) {
    const int exists = nbt_fs_exists(fs, NBT_FS_SETUP_LAUNCHER, "manager", NULL);
    if (exists != 0) {
        return exists > 0 ? 0 : -1;
    }
    const int fd = prepareNext(fs, NBT_FS_SETUP_LAUNCHER_NEXT);
    if (fd < 0) {
        return -1;
    }
    checkpoint("launcher", "created");
    if (nbt_release_copy_component(releaseFd, release, 0, fd, NULL, 0) || fchmod(fd, 0700)) {
        close(fd);
        return -1;
    }
    checkpoint("launcher", "written");
    if (fsync(fd)) {
        close(fd);
        return -1;
    }
    checkpoint("launcher", "synced");
    if (close(fd) || nbt_fs_setup_publish(fs, NBT_FS_SETUP_LAUNCHER_NEXT) ||
        nbt_fs_setup_sync(fs)) {
        return -1;
    }
    fault("launcher");
    return 0;
}

int publishUpload(nbt_fs *fs, int input, uint64_t length) {
    int exists = nbt_fs_exists(fs, NBT_FS_MANAGER_UPLOAD, "manager", NULL);
    if (exists != 0) {
        return exists > 0 ? 0 : -1;
    }
    exists = nbt_fs_exists(fs, NBT_FS_MANAGER_UPLOAD_NEXT, "manager", NULL);
    if (exists < 0 || (exists && (nbt_fs_unlink(fs, NBT_FS_MANAGER_UPLOAD_NEXT, "manager", NULL) ||
                                  nbt_fs_setup_sync(fs)))) {
        return -1;
    }
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_UPLOAD_NEXT, "manager", NULL, NBT_FS_CREATE);
    if (fd < 0) {
        return -1;
    }
    checkpoint("upload", "created");
    if (SlotFs::copyStream(input, fd, length, false)) {
        close(fd);
        return -1;
    }
    checkpoint("upload", "written");
    if (fsync(fd)) {
        close(fd);
        return -1;
    }
    checkpoint("upload", "synced");
    if (close(fd) ||
        nbt_fs_rename(fs, NBT_FS_MANAGER_UPLOAD_NEXT, NBT_FS_MANAGER_UPLOAD, "manager", NULL) ||
        nbt_fs_setup_sync(fs)) {
        return -1;
    }
    fault("upload");
    return 0;
}

bool preflightAll(nbt_fs *fs, size_t keyLen, const unsigned char keyHash[32], size_t profLen,
                  const unsigned char profHash[32], const nbt_release &release, uint64_t relSize) {
    return !SlotFs::preflightFile(fs, NBT_FS_MANAGER_KEY, keyLen, keyHash, false) &&
           !SlotFs::preflightFile(fs, NBT_FS_MANAGER_PROFILE, profLen, profHash, false) &&
           !SlotFs::preflightFile(fs, NBT_FS_SETUP_LAUNCHER, release.components[0].size,
                                  release.components[0].sha256, true) &&
           !SlotFs::preflightFile(fs, NBT_FS_MANAGER_UPLOAD, relSize, release.sha256, false);
}

bool existingMatchesInitial(nbt_fs *fs, const nbt_manager_state &state,
                            const nbt_release &release) {
    return nbt_fs_exists(fs, NBT_FS_MANAGER_KEY, "manager", NULL) == 1 &&
           nbt_fs_exists(fs, NBT_FS_MANAGER_PROFILE, "manager", NULL) == 1 &&
           nbt_fs_exists(fs, NBT_FS_SETUP_LAUNCHER, "manager", NULL) == 1 && !state.pending &&
           state.active.slot == 0 && state.active.sequence == release.sequence &&
           nbt_constant_equal(state.active.release_hash, release.sha256, 32) &&
           !nbt_manager_verify_slot(fs, &state.active);
}

} // namespace

int nbt_manager_setup(nbt_fs *fs, int release_fd, int key_fd, const char *compat,
                      const unsigned char expected_hash[32], char *error, unsigned error_size) {
    unsigned char key[kKeyMax], keyHash[32], profileHash[32];
    char profile[128];
    size_t keyLength = 0;
    struct stat releaseStat;
    nbt_release release;
    nbt_manager_state state;
    if (!fs || !expected_hash || !nbt_token_valid(compat, 48, NBT_TOKEN_DOT) ||
        readKey(key_fd, key, &keyLength) || fstat(release_fd, &releaseStat) ||
        releaseStat.st_size <= 0 ||
        nbt_release_verify(release_fd, key_fd, compat, &release, NULL, 0) ||
        !nbt_constant_equal(release.sha256, expected_hash, 32)) {
        return SlotFs::fail(error, error_size, "invalid initial release or key");
    }
    const int profileLength =
        snprintf(profile, sizeof(profile), "NBTMANAGERPROFILE1\ncompat=%s\n", compat);
    if (profileLength <= 0 || static_cast<size_t>(profileLength) >= sizeof(profile)) {
        return SlotFs::fail(error, error_size, "invalid initial profile");
    }
    SHA256(key, keyLength, keyHash);
    SHA256(reinterpret_cast<const unsigned char *>(profile), static_cast<size_t>(profileLength),
           profileHash);
    const uint64_t relSize = static_cast<uint64_t>(releaseStat.st_size);
    const uint64_t need = relSize * 2 + release.components[0].size + release.components[1].size +
                          release.components[2].size +
                          (release.component_count > NBT_RELEASE_CAPTURE_INDEX
                               ? release.components[NBT_RELEASE_CAPTURE_INDEX].size
                               : 0) +
                          kKeyMax;
    if (SlotFs::requireSpace(fs->root, need, 8)) {
        return SlotFs::fail(error, error_size, "insufficient setup space");
    }
    const size_t profLen = static_cast<size_t>(profileLength);
    if (!preflightAll(fs, keyLength, keyHash, profLen, profileHash, release, relSize)) {
        return SlotFs::fail(error, error_size, "conflicting manager setup files");
    }
    const int current = nbt_fs_exists(fs, NBT_FS_MANAGER_STATE, "manager", NULL);
    if (current < 0 && errno != ENOENT) {
        return SlotFs::fail(error, error_size, "unsafe manager state");
    }
    if (current > 0 &&
        (nbt_manager_store_inspect(fs, &state) || !existingMatchesInitial(fs, state, release))) {
        return SlotFs::fail(error, error_size, "existing manager does not match initial release");
    }
    if (nbt_fs_setup_layout(fs)) {
        return SlotFs::fail(error, error_size, "invalid setup layout");
    }
    int operation = nbt_fs_manager_operation_lock(fs);
    if (operation < 0) {
        return SlotFs::fail(error, error_size, "manager operation is in progress");
    }
    int result = -1;
    if (!preflightAll(fs, keyLength, keyHash, profLen, profileHash, release, relSize)) {
        SlotFs::fail(error, error_size, "conflicting manager setup files");
    } else if (!nbt_manager_store_load(fs, &state)) {
        if (!existingMatchesInitial(fs, state, release)) {
            SlotFs::fail(error, error_size, "existing manager does not match initial release");
        } else {
            result = 0;
        }
    } else if (errno != ENOENT && errno != EBUSY) {
        SlotFs::fail(error, error_size, "manager state requires recovery");
    } else if (publishBytes(fs, NBT_FS_MANAGER_KEY, NBT_FS_SETUP_KEY_NEXT, key, keyLength, "key") ||
               publishBytes(fs, NBT_FS_MANAGER_PROFILE, NBT_FS_SETUP_PROFILE_NEXT, profile, profLen,
                            "profile") ||
               publishLauncher(fs, release_fd, &release) ||
               publishUpload(fs, release_fd, relSize)) {
        SlotFs::fail(error, error_size, "cannot persist initial manager files");
    } else if (close(operation)) {
        operation = -1;
        SlotFs::fail(error, error_size, "cannot release setup lock");
    } else {
        operation = -1;
        if (!nbt_manager_stage(fs, 1, 0, expected_hash, error, error_size)) {
            result = 0;
        }
    }
    if (operation >= 0) {
        close(operation);
    }
    if (!result && error && error_size) {
        *error = 0;
    }
    return result;
}

namespace nbt {
namespace launcher {

core::Status ManagerSetup::setup(int releaseFd, int keyFd, const char *compat,
                                 const unsigned char expectedHash[32], char *error,
                                 unsigned errorSize) const {
    return nbt_manager_setup(const_cast<nbt_fs *>(root_.legacy()), releaseFd, keyFd, compat,
                             expectedHash, error, errorSize)
               ? core::Status::fromErrno("manager setup failed")
               : core::Status();
}

} // namespace launcher
} // namespace nbt
