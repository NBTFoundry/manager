// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/plugin_installer.hpp"

#include "nbt/manager_plugin.h"
#include "nbt/util.h"

#include <errno.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace launcher {
namespace {

core::Status fail(char *error, unsigned size, const char *message) {
    if (error && size) {
        size_t count = strlen(message);
        if (count >= size) {
            count = size - 1;
        }
        memcpy(error, message, count);
        error[count] = 0;
    }
    return core::Status::error(errno ? errno : EINVAL, message);
}

void fault(const char *step, unsigned index) {
#ifdef NBT_TESTING
    char name[64];
    const char *requested = getenv("NBT_HMI_SETUP_FAULT");
    if (snprintf(name, sizeof(name), "%s-%u", step, index) > 0 && requested &&
        !strcmp(name, requested)) {
        _exit(91);
    }
#else
    (void)step;
    (void)index;
#endif
}

int matches(nbt_hmi_fs &hmi, int temporary, unsigned index,
            const nbt_release_component &component) {
    unsigned char data[8192];
    unsigned char digest[32];
    SHA256_CTX sha;
    struct stat st;
    uint64_t used = 0;
    const int fd = nbt_hmi_open(&hmi, temporary, index, 0);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st) || st.st_size < 0 || static_cast<uint64_t>(st.st_size) != component.size ||
        (st.st_mode & 0777) != (index ? 0644 : 0755)) {
        close(fd);
        return -1;
    }
    SHA256_Init(&sha);
    while (used < component.size) {
        const size_t want = component.size - used < sizeof(data)
                                ? static_cast<size_t>(component.size - used)
                                : sizeof(data);
        const ssize_t count = read(fd, data, want);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            close(fd);
            return -1;
        }
        SHA256_Update(&sha, data, static_cast<size_t>(count));
        used += static_cast<size_t>(count);
    }
    if (close(fd)) {
        return -1;
    }
    SHA256_Final(digest, &sha);
    return nbt_constant_equal(digest, component.sha256, 32) ? 0 : -1;
}

} // namespace

core::Status RealReleaseVerifier::verify(int releaseFd, int keyFd, const char *compat,
                                         nbt_release &release, char *error, unsigned errorSize) {
    if (nbt_release_verify(releaseFd, keyFd, compat, &release, error, errorSize)) {
        return core::Status::error(errno ? errno : EINVAL, "release verification failed");
    }
    return core::Status();
}

core::Status PluginInstaller::preflight(nbt_hmi_fs &hmi, int releaseFd, int keyFd,
                                        const char *compat, const unsigned char digest[32],
                                        char *error, unsigned errorSize) {
    nbt_release release;
    uint64_t size = 0;
    if (!digest || !nbt_token_valid(compat, 48, NBT_TOKEN_DOT) ||
        !verifier_.verify(releaseFd, keyFd, compat, release, NULL, 0).ok() ||
        !nbt_constant_equal(release.sha256, digest, 32)) {
        return fail(error, errorSize, "invalid manager plugin release");
    }
    for (unsigned i = 0; i < NBT_HMI_FILES; ++i) {
        size += release.components[i + 3].size;
    }
    if (nbt_hmi_space(&hmi, size)) {
        return fail(error, errorSize, "insufficient HMI storage");
    }
    const int final = nbt_hmi_entries(&hmi, 0);
    const int temporary = nbt_hmi_entries(&hmi, 1);
    if (final < 0 || temporary < 0) {
        return fail(error, errorSize, "unsafe HMI plugin directory");
    }
    if (final) {
        for (unsigned i = 0; i < NBT_HMI_FILES; ++i) {
            if (matches(hmi, 0, i, release.components[i + 3])) {
                return fail(error, errorSize, "existing HMI plugin conflicts with release");
            }
        }
    }
    if (error && errorSize) {
        *error = 0;
    }
    return core::Status();
}

core::Status PluginInstaller::install(nbt_hmi_fs &hmi, int releaseFd, int keyFd, const char *compat,
                                      const unsigned char digest[32], char *error,
                                      unsigned errorSize) {
    nbt_release release;
    core::Status status = preflight(hmi, releaseFd, keyFd, compat, digest, error, errorSize);
    if (!status.ok()) {
        return status;
    }
    if (!verifier_.verify(releaseFd, keyFd, compat, release, NULL, 0).ok() ||
        !nbt_constant_equal(release.sha256, digest, 32)) {
        return fail(error, errorSize, "manager plugin release changed");
    }
    const int final = nbt_hmi_entries(&hmi, 0);
    if (final < 0) {
        return fail(error, errorSize, "unsafe HMI plugin directory");
    }
    if (final) {
        if (nbt_hmi_sync(&hmi, 0)) {
            return fail(error, errorSize, "cannot sync existing HMI plugin");
        }
        if (error && errorSize) {
            *error = 0;
        }
        return core::Status();
    }
    if (nbt_hmi_prepare(&hmi)) {
        return fail(error, errorSize, "cannot prepare HMI plugin directory");
    }
    status = writeTemporary(hmi, releaseFd, release, error, errorSize);
    if (!status.ok()) {
        return status;
    }
    if (nbt_hmi_publish(&hmi)) {
        return fail(error, errorSize, "cannot publish HMI plugin");
    }
    fault("published", 0);
    if (error && errorSize) {
        *error = 0;
    }
    return core::Status();
}

core::Status PluginInstaller::refresh(nbt_hmi_fs &hmi, int releaseFd, int keyFd, const char *compat,
                                      const unsigned char digest[32], bool &changed, char *error,
                                      unsigned errorSize) {
    nbt_release release;
    changed = false;
    if (!digest || !verifier_.verify(releaseFd, keyFd, compat, release, NULL, 0).ok() ||
        !nbt_constant_equal(release.sha256, digest, 32)) {
        return fail(error, errorSize, "invalid manager plugin release");
    }
    if (nbt_hmi_entries(&hmi, 0) == 1 && nbt_hmi_drop_old(&hmi)) {
        return fail(error, errorSize, "cannot remove the replaced HMI plugin");
    }
    const int live = nbt_hmi_entries(&hmi, 0);
    if (live < 0 || nbt_hmi_entries(&hmi, 1) < 0) {
        return fail(error, errorSize, "unsafe HMI plugin directory");
    }
    if (!live) { // none, or a replacement interrupted between its renames
        core::Status status = install(hmi, releaseFd, keyFd, compat, digest, error, errorSize);
        if (status.ok() && nbt_hmi_drop_old(&hmi)) {
            return fail(error, errorSize, "cannot remove the replaced HMI plugin");
        }
        changed = status.ok();
        return status;
    }
    unsigned same = 0;
    uint64_t size = 0;
    for (unsigned i = 0; i < NBT_HMI_FILES; ++i) {
        same += matches(hmi, 0, i, release.components[i + 3]) ? 0u : 1u;
        size += release.components[i + 3].size;
    }
    if (same == NBT_HMI_FILES) {
        return core::Status();
    }
    if (nbt_hmi_space(&hmi, size)) {
        return fail(error, errorSize, "insufficient HMI storage");
    }
    if (nbt_hmi_prepare_replace(&hmi)) {
        return fail(error, errorSize, "cannot prepare HMI plugin directory");
    }
    core::Status status = writeTemporary(hmi, releaseFd, release, error, errorSize);
    if (!status.ok()) {
        return status;
    }
    if (nbt_hmi_replace(&hmi)) {
        return fail(error, errorSize, "cannot replace HMI plugin");
    }
    changed = true;
    if (error && errorSize) {
        *error = 0;
    }
    return core::Status();
}

/* Writes and checks every role in the temporary folder, keeping a file that
 * already matches (left by an interrupted attempt). */
core::Status PluginInstaller::writeTemporary(nbt_hmi_fs &hmi, int releaseFd,
                                             const nbt_release &release, char *error,
                                             unsigned errorSize) {
    for (unsigned i = 0; i < NBT_HMI_FILES; ++i) {
        int fd = nbt_hmi_open(&hmi, 1, i, 0);
        if (fd >= 0) {
            if (close(fd)) {
                return fail(error, errorSize, "cannot inspect HMI plugin file");
            }
            if (!matches(hmi, 1, i, release.components[i + 3])) {
                continue;
            }
            if (nbt_hmi_discard(&hmi, i)) {
                return fail(error, errorSize, "unsafe temporary HMI file");
            }
        } else if (errno != ENOENT) {
            return fail(error, errorSize, "unsafe temporary HMI file");
        }
        fd = nbt_hmi_open(&hmi, 1, i, 1);
        if (fd < 0) {
            return fail(error, errorSize, "cannot create HMI plugin file");
        }
        fault("created", i);
        if (nbt_release_copy_component(releaseFd, &release, i + 3, fd, NULL, 0) ||
            fchmod(fd, i ? 0644 : 0755)) {
            close(fd);
            return fail(error, errorSize, "cannot write HMI plugin file");
        }
        fault("written", i);
        if (fsync(fd)) {
            close(fd);
            return fail(error, errorSize, "cannot sync HMI plugin file");
        }
        fault("synced", i);
        if (close(fd) || matches(hmi, 1, i, release.components[i + 3])) {
            return fail(error, errorSize, "HMI plugin file verification failed");
        }
    }
    return core::Status();
}

} // namespace launcher
} // namespace nbt

int nbt_manager_plugin_preflight(nbt_hmi_fs *hmi, int release_fd, int key_fd, const char *compat,
                                 const unsigned char digest[32], char *error, unsigned error_size) {
    if (!hmi) {
        return -1;
    }
    nbt::launcher::RealReleaseVerifier verifier;
    nbt::launcher::PluginInstaller installer(verifier);
    return installer.preflight(*hmi, release_fd, key_fd, compat, digest, error, error_size).ok()
               ? 0
               : -1;
}

int nbt_manager_plugin_install(nbt_hmi_fs *hmi, int release_fd, int key_fd, const char *compat,
                               const unsigned char digest[32], char *error, unsigned error_size) {
    if (!hmi) {
        return -1;
    }
    nbt::launcher::RealReleaseVerifier verifier;
    nbt::launcher::PluginInstaller installer(verifier);
    return installer.install(*hmi, release_fd, key_fd, compat, digest, error, error_size).ok() ? 0
                                                                                               : -1;
}
