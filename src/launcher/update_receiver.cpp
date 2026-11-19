// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/update_receiver.hpp"

#include "nbt/manager_receive.h"
#include "nbt/manager_stage.h"
#include "nbt/manager_store.h"
#include "nbt/package.h"
#include "nbt/release.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define MAX_RELEASE                                                                                \
    ((uint64_t)NBT_RELEASE_PAYLOAD_MAX + NBT_RELEASE_MANIFEST_MAX + NBT_SIGNATURE_SIZE + 16u)
#define IDLE_MS 15000u
#define TOTAL_MS 600000u

static int injected_error(const char *step) {
#ifdef NBT_TESTING
    const char *requested = getenv("NBT_MANAGER_RECEIVE_ERROR");
    if (requested && !strcmp(requested, step)) {
        errno = !strcmp(step, "write") ? ENOSPC : EIO;
        return 1;
    }
#else
    (void)step;
#endif
    return 0;
}

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

static int barrier(nbt_fs *fs) {
    int fd = nbt_fs_open(fs, NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0) {
        return -1;
    }
    if (injected_error("barrier") || fsync(fd)) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    if (close(fd)) {
        return -1;
    }
#ifndef __QNX__
    {
        char path[NBT_PATH_MAX];
        if (nbt_fs_path(fs, NBT_FS_STATE_DIR, "manager", NULL, path, sizeof(path))) {
            return -1;
        }
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            return -1;
        }
        if (fsync(fd)) {
            int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }
        if (close(fd)) {
            return -1;
        }
    }
#endif
    return 0;
}

static int transfer(int input, int output, uint64_t remaining) {
    unsigned char data[8192];
    uint64_t start, current, last;
    if (input < 0 || input >= FD_SETSIZE || nbt_monotonic_ms(&start) ||
        start > UINT64_MAX - TOTAL_MS) {
        return -1;
    }
    last = start;
    for (;;) {
        fd_set readfds;
        struct timeval timeout;
        uint64_t deadline, idle_deadline;
        size_t wanted = remaining < sizeof(data) ? (size_t)remaining : sizeof(data);
        ssize_t n;
        if (nbt_monotonic_ms(&current) || current < last || current >= start + TOTAL_MS ||
            last > UINT64_MAX - IDLE_MS || current >= last + IDLE_MS) {
            errno = ETIMEDOUT;
            return -1;
        }
        deadline = start + TOTAL_MS;
        idle_deadline = last + IDLE_MS;
        if (idle_deadline < deadline) {
            deadline = idle_deadline;
        }
        timeout.tv_sec = (long)((deadline - current) / 1000);
        timeout.tv_usec = (long)((deadline - current) % 1000) * 1000;
        FD_ZERO(&readfds);
        FD_SET(input, &readfds);
        n = select(input + 1, &readfds, NULL, NULL, &timeout);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            if (!n) {
                errno = ETIMEDOUT;
            }
            return -1;
        }
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
            size_t used = 0;
            while (used < (size_t)n) {
                ssize_t written;
                if (injected_error("write")) {
                    return -1;
                }
                written = write(output, data + used, (size_t)n - used);
                if (written < 0 && errno == EINTR) {
                    continue;
                }
                if (written <= 0) {
                    return -1;
                }
                used += (size_t)written;
            }
        }
        remaining -= (size_t)n;
        if (nbt_monotonic_ms(&last)) {
            return -1;
        }
    }
}

int nbt_manager_receive_update(nbt_fs *fs, int input_fd, uint64_t size,
                               const unsigned char expected_hash[32], int recovery, char *error,
                               unsigned error_size) {
    nbt_manager_state state;
    struct statvfs space;
    uint64_t blocks;
    int operation = -1, output = -1, verify = -1, next, result = -1;
    if (!fs || !expected_hash || !size || size > MAX_RELEASE || (recovery != 0 && recovery != 1)) {
        return fail(error, error_size, "invalid release transfer");
    }
    operation = nbt_fs_manager_operation_lock(fs);
    if (operation < 0) {
        return fail(error, error_size, "manager operation is in progress");
    }
    if (nbt_manager_discard_candidate(fs) < 0 || nbt_manager_store_load(fs, &state) ||
        state.pending) {
        fail(error, error_size, "manager state requires recovery");
        goto done;
    }
    if (statvfs(fs->root, &space) || !space.f_frsize ||
        (blocks = size / space.f_frsize + (size % space.f_frsize != 0) + 4) < 4 ||
        space.f_bavail < blocks) {
        fail(error, error_size, "insufficient upload space");
        goto done;
    }
    next = nbt_fs_exists(fs, NBT_FS_MANAGER_UPLOAD_NEXT, "manager", NULL);
    if (next < 0 || (next && nbt_fs_unlink(fs, NBT_FS_MANAGER_UPLOAD_NEXT, "manager", NULL))) {
        fail(error, error_size, "unsafe temporary upload");
        goto done;
    }
    output = nbt_fs_open(fs, NBT_FS_MANAGER_UPLOAD_NEXT, "manager", NULL, NBT_FS_CREATE);
    if (output < 0 || transfer(input_fd, output, size) || injected_error("flush") ||
        fsync(output)) {
        fail(error, error_size, "release transfer failed");
        goto done;
    }
    if (close(output)) {
        output = -1;
        fail(error, error_size, "release transfer failed");
        goto done;
    }
    output = -1;
    verify = nbt_fs_open(fs, NBT_FS_MANAGER_UPLOAD_NEXT, "manager", NULL, NBT_FS_READ);
    if (verify < 0 || nbt_manager_verify_release(fs, verify, expected_hash)) {
        fail(error, error_size, "release signature or digest rejected");
        goto done;
    }
    if (close(verify)) {
        verify = -1;
        fail(error, error_size, "release verification failed");
        goto done;
    }
    verify = -1;
    if (injected_error("rename") ||
        nbt_fs_rename(fs, NBT_FS_MANAGER_UPLOAD_NEXT, NBT_FS_MANAGER_UPLOAD, "manager", NULL) ||
        barrier(fs)) {
        fail(error, error_size, "cannot persist release upload");
        goto done;
    }
    if (close(operation)) {
        operation = -1;
        fail(error, error_size, "cannot release operation lock");
        goto done;
    }
    operation = -1;
    result = nbt_manager_stage(fs, 0, recovery, expected_hash, error, error_size);
done:
    if (verify >= 0) {
        close(verify);
    }
    if (output >= 0) {
        close(output);
    }
    if (operation >= 0) {
        close(operation);
    }
    return result;
}

namespace nbt {
namespace launcher {

core::Status UpdateReceiver::receive(int inputFd, uint64_t size,
                                     const unsigned char expectedHash[32], bool recovery,
                                     char *error, unsigned errorSize) const {
    return nbt_manager_receive_update(const_cast<nbt_fs *>(root_.legacy()), inputFd, size,
                                      expectedHash, recovery, error, errorSize)
               ? core::Status::fromErrno("manager update receive failed")
               : core::Status();
}

} // namespace launcher
} // namespace nbt
