// SPDX-License-Identifier: MPL-2.0
/* The stable launcher and the iDrive plugin follow the confirmed manager
 * release. First setup (bootstrap.cpp) writes both once and never overwrites
 * them; a slot update replaces only the GUI and the worker. Without this, a car
 * set up before dev41 kept a launcher without --door and a plugin without the
 * wake poll until the manager was removed and installed again. */
#include "nbt/manager_refresh.h"

#include "nbt/launcher/plugin_installer.hpp"
#include "nbt/launcher/slot_fs.hpp"
#include "nbt/launcher/slot_verifier.hpp"
#include "nbt/manager_store.h"
#include "nbt/release.h"
#include "nbt/storage/capture_install.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using nbt::launcher::SlotFs;

int fail(char *error, unsigned size, const char *message) {
    SlotFs::fail(error, size, message);
    return -1;
}

void fault(const char *step) {
#ifdef NBT_TESTING
    const char *name = getenv("NBT_REFRESH_FAULT");
    if (name && !strcmp(name, step)) {
        _exit(91);
    }
#else
    (void)step;
#endif
}

/* Read-only comparison of one live plugin file, before anything is made
 * writable (nbt_hmi_check refuses a read-only partition). */
bool liveFileMatches(const char *hmiRoot, unsigned index, const nbt_release_component &component) {
    char path[NBT_PATH_MAX];
    unsigned char data[8192], digest[32];
    struct stat st;
    SHA256_CTX sha;
    uint64_t used = 0;
    if (snprintf(path, sizeof(path), "%s/Plugins/NbtManager/%s", hmiRoot, nbt_hmi_name(index)) >=
        static_cast<int>(sizeof(path))) {
        return false;
    }
    struct stat link;
    if (lstat(path, &link) || !S_ISREG(link.st_mode)) { // no O_NOFOLLOW on QNX 6.5
        return false;
    }
    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    bool ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_dev == link.st_dev &&
              st.st_ino == link.st_ino && static_cast<uint64_t>(st.st_size) == component.size;
    SHA256_Init(&sha);
    while (ok && used < component.size) {
        const ssize_t n = read(fd, data, sizeof(data));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        ok = n > 0;
        if (ok) {
            SHA256_Update(&sha, data, static_cast<size_t>(n));
            used += static_cast<uint64_t>(n);
        }
    }
    close(fd);
    SHA256_Final(digest, &sha);
    return ok && used == component.size && nbt_constant_equal(digest, component.sha256, 32);
}

/* Current: every live file matches and no replaced folder is left behind
 * (iDrive could take a leftover for a second plugin). */
bool pluginCurrent(const char *hmiRoot, const nbt_release &release) {
    char old[NBT_PATH_MAX];
    struct stat st;
    if (snprintf(old, sizeof(old), "%s/Plugins/.NbtManager.old", hmiRoot) >=
            static_cast<int>(sizeof(old)) ||
        !lstat(old, &st) || errno != ENOENT) {
        return false;
    }
    for (unsigned i = 0; i < NBT_HMI_FILES; ++i) {
        if (!liveFileMatches(hmiRoot, i, release.components[i + 3])) {
            return false;
        }
    }
    return true;
}

bool launcherCurrent(nbt_fs *fs, const nbt_release &release) {
    const nbt_release_component &launcher = release.components[0];
    return !SlotFs::fileMatches(fs, NBT_FS_SETUP_LAUNCHER, NULL, launcher.size, launcher.sha256,
                                true);
}

/* Writes the release's launcher beside the stable one, checks it, and renames
 * it over: a running launcher keeps its old file, the next start uses the new. */
int refreshLauncher(nbt_fs *fs, int releaseFd, const nbt_release &release) {
    const nbt_release_component &launcher = release.components[0];
    const int next = nbt_fs_exists(fs, NBT_FS_SETUP_LAUNCHER_NEXT, "manager", NULL);
    if (next < 0 || (next && nbt_fs_setup_discard(fs, NBT_FS_SETUP_LAUNCHER_NEXT))) {
        return -1;
    }
    const int fd = nbt_fs_setup_create(fs, NBT_FS_SETUP_LAUNCHER_NEXT);
    if (fd < 0) {
        return -1;
    }
    if (nbt_release_copy_component(releaseFd, &release, 0, fd, NULL, 0) || fchmod(fd, 0700) ||
        fsync(fd)) {
        close(fd);
        return -1;
    }
    if (close(fd) || SlotFs::fileMatches(fs, NBT_FS_SETUP_LAUNCHER_NEXT, NULL, launcher.size,
                                         launcher.sha256, true)) {
        return -1;
    }
    fault("launcher-written");
    return nbt_fs_rename(fs, NBT_FS_SETUP_LAUNCHER_NEXT, NBT_FS_SETUP_LAUNCHER, "manager", NULL) ||
                   nbt_fs_setup_sync(fs)
               ? -1
               : 0;
}

/* Plugin replacement between the two remounts. */
int refreshPlugin(const char *hmiRoot, int releaseFd, int keyFd, const char *compat,
                  const nbt_release &release, bool &changed, char *error, unsigned errorSize) {
    nbt_hmi_fs hmi;
    const char *reason = NULL;
    char text[96];
    if (nbt_hmi_check(&hmi, hmiRoot, &reason)) {
        snprintf(text, sizeof(text), "HMI layout: %s", reason ? reason : "unsupported");
        return fail(error, errorSize, text);
    }
    nbt::launcher::RealReleaseVerifier verifier;
    nbt::launcher::PluginInstaller installer(verifier);
    if (!installer.refresh(hmi, releaseFd, keyFd, compat, release.sha256, changed, error, errorSize)
             .ok()) {
        if (error && errorSize && !*error) {
            fail(error, errorSize, "cannot replace the iDrive plugin");
        }
        return -1;
    }
    return 0;
}

/* The operation lock: free, or held by the launcher that started this worker. */
int operationLock(nbt_fs *fs) {
    const int lock = nbt_fs_manager_operation_lock(fs);
    return lock >= 0 ? lock : nbt_fs_manager_operation_parent(fs);
}

int refreshLocked(nbt_fs *fs, const char *hmiRoot, nbt_remount_fn remount, bool staged,
                  unsigned *changed, char *error, unsigned errorSize) {
    nbt_manager_state state;
    nbt_release release;
    char compat[49], slot[2] = {0, 0};
    if (nbt_manager_store_inspect(fs, &state) || state.active.slot > 1) {
        return fail(error, errorSize, "manager state unreadable");
    }
    if (state.pending && !staged) {
        return 1;
    }
    slot[0] = static_cast<char>('0' + state.active.slot);
    if (nbt::launcher::SlotVerifier::readProfileCompat(fs, compat)) {
        return fail(error, errorSize, "invalid manager profile");
    }
    const int key = nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ);
    const int file = nbt_fs_open(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot, NBT_FS_READ);
    int result = -1;
    bool launcherOld = false;
    if (key < 0 || file < 0 || nbt_release_verify(file, key, compat, &release, NULL, 0) ||
        !nbt_constant_equal(release.sha256, state.active.release_hash, 32)) {
        fail(error, errorSize, "active manager release failed verification");
    } else if ((launcherOld = !launcherCurrent(fs, release)) &&
               (refreshLauncher(fs, file, release) || !launcherCurrent(fs, release))) {
        fail(error, errorSize, "cannot replace the launcher");
    } else {
        *changed |= launcherOld ? NBT_REFRESH_LAUNCHER : 0u;
        result = 0;
        if (!pluginCurrent(hmiRoot, release)) {
            if (remount && remount(1)) {
                result = fail(error, errorSize, "cannot make the iDrive plugin folder writable");
            } else {
                bool pluginChanged = false;
                result = refreshPlugin(hmiRoot, file, key, compat, release, pluginChanged, error,
                                       errorSize);
                if (remount && remount(0) && !result) {
                    result = fail(error, errorSize,
                                  "cannot make the iDrive plugin folder "
                                  "read-only again");
                }
                *changed |= !result && pluginChanged ? NBT_REFRESH_PLUGIN : 0u;
            }
        }
        if (result == 0) {
            const char *starterCfg = "/opt/sys/etc/starter.cfg";
#ifdef NBT_TESTING
            starterCfg = getenv("NBT_TEST_STARTER_CFG");
#endif
            const bool wantCapture = release.component_count > NBT_RELEASE_CAPTURE_INDEX;
            if (!starterCfg) {
                if (wantCapture) {
                    result = fail(error, errorSize, "starter.cfg test path not configured");
                }
            } else {
                int capChanged = 0;
                result = nbt_capture_refresh_from_release(fs, hmiRoot, starterCfg, file, &release,
                                                          remount, &capChanged, error, errorSize);
                if (result == NBT_CAPTURE_REFUSED) { // the rest of the update stands
                    *changed |= NBT_REFRESH_CAPTURE_REFUSED;
                    result = 0;
                }
                *changed |= !result && capChanged ? NBT_REFRESH_CAPTURE : 0u;
            }
        }
    }
    if (key >= 0) {
        close(key);
    }
    if (file >= 0) {
        close(file);
    }
    return result;
}

} // namespace

int nbt_manager_launcher_current(nbt_fs *fs, char version[49]) {
    nbt_manager_state state;
    nbt_release release;
    char compat[49], slot[2] = {0, 0};
    if (!fs || !version || nbt_manager_store_inspect(fs, &state) || state.active.slot > 1 ||
        nbt::launcher::SlotVerifier::readProfileCompat(fs, compat)) {
        return -1;
    }
    slot[0] = static_cast<char>('0' + state.active.slot);
    const int key = nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ);
    const int file = nbt_fs_open(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot, NBT_FS_READ);
    const bool verified = key >= 0 && file >= 0 &&
                          !nbt_release_verify(file, key, compat, &release, NULL, 0) &&
                          nbt_constant_equal(release.sha256, state.active.release_hash, 32);
    if (key >= 0) {
        close(key);
    }
    if (file >= 0) {
        close(file);
    }
    if (!verified) {
        return -1;
    }
    memcpy(version, release.version, sizeof(release.version));
    return launcherCurrent(fs, release) ? 1 : 0;
}

int nbt_manager_refresh_installed(nbt_fs *fs, const char *hmi_root, nbt_remount_fn remount,
                                  unsigned *changed, char *error, unsigned error_size) {
    if (error && error_size) {
        *error = 0;
    }
    if (!fs || !hmi_root || !changed) {
        return fail(error, error_size, "invalid refresh request");
    }
    *changed = 0;
    const int lock = operationLock(fs);
    if (lock < 0) {
        return fail(error, error_size, "manager operation is in progress");
    }
    const int result = refreshLocked(fs, hmi_root, remount, false, changed, error, error_size);
    close(lock);
    return result;
}

int nbt_manager_refresh_staged(nbt_fs *fs, const char *hmi_root, unsigned *changed, char *error,
                               unsigned error_size) {
    if (error && error_size) {
        *error = 0;
    }
    if (!fs || !hmi_root || !changed) {
        return fail(error, error_size, "invalid refresh request");
    }
    *changed = 0;
    const int lock = nbt_fs_manager_operation_lock(fs);
    if (lock < 0) {
        return fail(error, error_size, "manager operation is in progress");
    }
    const int result = refreshLocked(fs, hmi_root, NULL, true, changed, error, error_size);
    close(lock);
    return result;
}
