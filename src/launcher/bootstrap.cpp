// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/bootstrap.hpp"

#include "nbt/fs.h"
#include "nbt/manager_bootstrap.h"
#include "nbt/manager_plugin.h"
#include "nbt/manager_setup.h"
#include "nbt/package.h"
#include "nbt/release.h"
#include "nbt/target.h"
#include "nbt/util.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#ifdef __QNX__
#include <arm/cpuinline.h>
#include <sys/syspage.h>
#endif

#define BOOTSTRAP_KEY_MAX 16384u
#define BOOTSTRAP_RELEASE_MAX                                                                      \
    ((uint64_t)NBT_RELEASE_PAYLOAD_MAX + NBT_RELEASE_MANIFEST_MAX + NBT_SIGNATURE_SIZE + 16u)
#define IDLE_MS 15000u
#define TOTAL_MS 600000u

static int fail(char *error, unsigned size, const char *message) {
    if (error && size) {
        size_t n = strlen(message);
        if (n >= size) {
            n = size - 1;
        }
        memcpy(error, message, n);
        error[n] = 0;
    }
    return -1;
}

/* Read exactly two bounded files and EOF; the same total and idle deadlines
 * apply to the complete stream. The files are anonymous and automatically
 * removed when the process closes them. */
int nbt_manager_receive_pair(int input, int key, uint64_t key_size, int release,
                             uint64_t release_size) {
    unsigned char data[8192];
    uint64_t start, last, now;
    int output = key;
    uint64_t remaining = key_size;
    if (input < 0 || input >= FD_SETSIZE || nbt_monotonic_ms(&start) ||
        start > UINT64_MAX - TOTAL_MS) {
        return -1;
    }
    last = start;
    for (;;) {
        fd_set fds;
        struct timeval timeout;
        uint64_t deadline;
        size_t wanted;
        ssize_t n;
        if (!remaining && output == key) {
            output = release;
            remaining = release_size;
        }
        if (nbt_monotonic_ms(&now) || now < last || now >= start + TOTAL_MS ||
            last > UINT64_MAX - IDLE_MS || now >= last + IDLE_MS) {
            errno = ETIMEDOUT;
            return -1;
        }
        deadline = start + TOTAL_MS;
        if (last + IDLE_MS < deadline) {
            deadline = last + IDLE_MS;
        }
        timeout.tv_sec = (long)((deadline - now) / 1000);
        timeout.tv_usec = (long)((deadline - now) % 1000) * 1000;
        FD_ZERO(&fds);
        FD_SET(input, &fds);
        n = select(input + 1, &fds, NULL, NULL, &timeout);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            if (!n) {
                errno = ETIMEDOUT;
            }
            return -1;
        }
        wanted = remaining < sizeof(data) ? (size_t)remaining : sizeof(data);
        n = read(input, data, wanted ? wanted : 1);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (n < 0 || (!remaining && n > 0) || (remaining && n == 0)) {
            return -1;
        }
        if (!remaining) {
            return 0;
        }
        {
            size_t done = 0;
            while (done < (size_t)n) {
                ssize_t written = write(output, data + done, (size_t)n - done);
                if (written < 0 && errno == EINTR) {
                    continue;
                }
                if (written <= 0) {
                    return -1;
                }
                done += (size_t)written;
            }
        }
        remaining -= (size_t)n;
        if (nbt_monotonic_ms(&last)) {
            return -1;
        }
    }
}

int nbt_manager_platform_supported(void) {
#ifndef NBT_TESTING
    struct utsname platform;
    int armv7 = 0;
#if defined(__QNX__) && defined(ARM_CPU_FLAG_V7)
    armv7 = (__cpu_flags & ARM_CPU_FLAG_V7) != 0;
#endif
    return !uname(&platform) && nbt_target_compatible(platform.sysname, platform.release, armv7);
#else
    return 1;
#endif
}

int nbt_manager_bootstrap(const char *root, int input, uint64_t key_size, uint64_t release_size,
                          const char *compat, const unsigned char digest[32], char *error,
                          unsigned error_size) {
    FILE *key = NULL, *release = NULL;
    nbt_release verified;
    nbt_fs fs;
    nbt_hmi_fs hmi;
    const char *hmi_root = "/opt/hmi/ID5/data/ro";
    int result = -1, operation = -1;
    uint64_t required;
    if (error && error_size) {
        *error = 0;
    }
    if (!root || !digest || !nbt_token_valid(compat, 48, NBT_TOKEN_DOT) || !key_size ||
        key_size > BOOTSTRAP_KEY_MAX || !release_size || release_size > BOOTSTRAP_RELEASE_MAX) {
        return fail(error, error_size, "invalid bootstrap request");
    }
    if (!nbt_manager_platform_supported()) {
        return fail(error, error_size, "unsupported platform");
    }
    key = nbt_fs_bootstrap_temp();
    release = nbt_fs_bootstrap_temp();
    if (!key || !release ||
        nbt_manager_receive_pair(input, fileno(key), key_size, fileno(release), release_size)) {
        fail(error, error_size, "bootstrap transfer failed");
        goto done;
    }
    if (nbt_release_verify(fileno(release), fileno(key), compat, &verified, NULL, 0) ||
        !nbt_constant_equal(verified.sha256, digest, 32)) {
        fail(error, error_size, "bootstrap signature or digest rejected");
        goto done;
    }
#ifdef NBT_TESTING
    hmi_root = getenv("NBT_TEST_HMI_ROOT");
    if (!hmi_root) {
        fail(error, error_size, "missing test HMI root");
        goto done;
    }
#endif
    {
        const char *reason = NULL;
        char text[96];
        if (nbt_hmi_check(&hmi, hmi_root, &reason)) {
            /* "HMI layout: <check>", so the installer can tell which one. */
            snprintf(text, sizeof(text), "HMI layout: %s", reason ? reason : "unsupported");
            fail(error, error_size, text);
            goto done;
        }
    }
    if (nbt_manager_plugin_preflight(&hmi, fileno(release), fileno(key), compat, digest, error,
                                     error_size)) {
        if (!error || !error_size || !*error) {
            fail(error, error_size, "HMI layout: unsupported");
        }
        goto done;
    }
    required = release_size * 2 + verified.components[0].size + verified.components[1].size +
               verified.components[2].size +
               (verified.component_count > NBT_RELEASE_CAPTURE_INDEX
                    ? verified.components[NBT_RELEASE_CAPTURE_INDEX].size
                    : 0) +
               BOOTSTRAP_KEY_MAX;
    if (nbt_fs_setup_root(&fs, root, required, error, error_size) ||
        nbt_manager_setup(&fs, fileno(release), fileno(key), compat, digest, error, error_size)) {
        goto done;
    }
    operation = nbt_fs_manager_operation_lock(&fs);
    if (operation < 0) {
        fail(error, error_size, "manager operation is in progress");
        goto done;
    }
    if (nbt_manager_plugin_install(&hmi, fileno(release), fileno(key), compat, digest, error,
                                   error_size)) {
        goto done;
    }
    result = 0;
done:
    if (operation >= 0 && close(operation)) {
        result = -1;
    }
    if (key && fclose(key)) {
        result = -1;
    }
    if (release && fclose(release)) {
        result = -1;
    }
    if (!result && error && error_size) {
        *error = 0;
    }
    return result;
}

namespace nbt {
namespace launcher {

core::Status Bootstrap::run(const char *root, int input, uint64_t keySize, uint64_t releaseSize,
                            const char *compat, const unsigned char digest[32], char *error,
                            unsigned errorSize) const {
    return nbt_manager_bootstrap(root, input, keySize, releaseSize, compat, digest, error,
                                 errorSize)
               ? core::Status::fromErrno("manager bootstrap failed")
               : core::Status();
}

} // namespace launcher
} // namespace nbt
