#include "nbt/launcher/slot_fs.hpp"

#include "nbt/storage/path_open.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace nbt {
namespace launcher {
namespace {

void stateFault(const char *step) {
#ifdef NBT_TESTING
    const char *requested = getenv("NBT_MANAGER_STATE_FAULT");
    if (requested && !strcmp(requested, step)) {
        _exit(91);
    }
#else
    (void)step;
#endif
}

} // namespace

int SlotFs::fail(char *error, unsigned size, const char *message) {
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

void SlotFs::stageFault(const char *step) {
#ifdef NBT_TESTING
    const char *requested = getenv("NBT_MANAGER_STAGE_FAULT");
    if (requested && !strcmp(requested, step)) {
        _exit(91);
    }
#else
    (void)step;
#endif
}

int SlotFs::injectedStageError(const char *step) {
#ifdef NBT_TESTING
    const char *requested = getenv("NBT_MANAGER_STAGE_ERROR");
    if (requested && !strcmp(requested, step)) {
        errno = !strcmp(step, "release-write") ? ENOSPC : EIO;
        return 1;
    }
#else
    (void)step;
#endif
    return 0;
}

int SlotFs::writeAll(int fd, const void *data, size_t size) {
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    while (size) {
        const ssize_t n = write(fd, bytes, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        bytes += n;
        size -= static_cast<size_t>(n);
    }
    return 0;
}

int SlotFs::copyStream(int inputFd, int outputFd, uint64_t length, bool injectReleaseWrite) {
    unsigned char bytes[8192];
    if (lseek(inputFd, 0, SEEK_SET) < 0) {
        return -1;
    }
    while (length) {
        const size_t want = length < sizeof(bytes) ? static_cast<size_t>(length) : sizeof(bytes);
        const ssize_t n = read(inputFd, bytes, want);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0 || (injectReleaseWrite && injectedStageError("release-write")) ||
            writeAll(outputFd, bytes, static_cast<size_t>(n))) {
            return -1;
        }
        length -= static_cast<size_t>(n);
    }
    return 0;
}

int SlotFs::requireSpace(const char *root, uint64_t need, uint64_t reserveBlocks) {
    struct statvfs space;
    if (statvfs(root, &space) || !space.f_frsize || need > UINT64_MAX - space.f_frsize) {
        return -1;
    }
    const uint64_t blocks = need / space.f_frsize + (need % space.f_frsize != 0) + reserveBlocks;
    return (blocks < reserveBlocks || space.f_bavail < blocks) ? -1 : 0;
}

int SlotFs::managerBarrier(nbt_fs *fs) {
    const int fd = nbt_fs_open(fs, NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0 || storage::SafePathOpen::syncWritableFd(fd)) {
        return -1;
    }
#ifndef __QNX__
    {
        char path[NBT_PATH_MAX];
        if (nbt_fs_path(fs, NBT_FS_STATE_DIR, "manager", NULL, path, sizeof(path)) ||
            storage::SafePathOpen::syncDirPath(path)) {
            return -1;
        }
    }
#endif
    return 0;
}

int SlotFs::slotBarrier(nbt_fs *fs, const char *slot) {
    const int fd = nbt_fs_open(fs, NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0) {
        return -1;
    }
    if (injectedStageError("slot-sync") || fsync(fd)) {
        const int saved = errno;
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
        if (nbt_fs_path(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot, path, sizeof(path)) ||
            nbt_fs_path(fs, NBT_FS_MANAGER_SLOT_DIR, "manager", slot, path, sizeof(path)) ||
            storage::SafePathOpen::syncDirPath(path)) {
            return -1;
        }
    }
#else
    (void)slot;
#endif
    return 0;
}

int SlotFs::fileMatches(nbt_fs *fs, nbt_fs_path_kind kind, const char *slot, uint64_t length,
                        const unsigned char digest[32], bool executable) {
    unsigned char bytes[8192];
    unsigned char actual[32];
    SHA256_CTX sha;
    struct stat st;
    uint64_t used = 0;
    const int fd = nbt_fs_open(fs, kind, "manager", slot, NBT_FS_READ);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st) || st.st_size < 0 || static_cast<uint64_t>(st.st_size) != length ||
        (((st.st_mode & S_IXUSR) != 0) != executable)) {
        close(fd);
        return -1;
    }
    SHA256_Init(&sha);
    while (used < length) {
        const size_t want =
            length - used < sizeof(bytes) ? static_cast<size_t>(length - used) : sizeof(bytes);
        const ssize_t n = read(fd, bytes, want);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            close(fd);
            return -1;
        }
        SHA256_Update(&sha, bytes, static_cast<size_t>(n));
        used += static_cast<size_t>(n);
    }
    if (close(fd)) {
        return -1;
    }
    SHA256_Final(actual, &sha);
    return nbt_constant_equal(actual, digest, 32) ? 0 : -1;
}

int SlotFs::preflightFile(nbt_fs *fs, nbt_fs_path_kind kind, uint64_t length,
                          const unsigned char digest[32], bool executable) {
    const int exists = nbt_fs_exists(fs, kind, "manager", NULL);
    if (exists < 0 && errno == ENOENT) {
        return 0;
    }
    if (exists < 0 || (exists && fileMatches(fs, kind, NULL, length, digest, executable))) {
        return -1;
    }
    return 0;
}

int SlotFs::readState(nbt_fs *fs, nbt_manager_state *state) {
    char text[NBT_MANAGER_STATE_MAX];
    size_t used = 0;
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_STATE, "manager", NULL, NBT_FS_READ);
    if (fd < 0) {
        return -1;
    }
    while (used < sizeof(text)) {
        const ssize_t n = read(fd, text + used, sizeof(text) - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            const int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }
        if (!n) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    if (close(fd)) {
        return -1;
    }
    if (used == sizeof(text) || nbt_manager_state_parse(text, used, state)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int SlotFs::writeState(nbt_fs *fs, const nbt_manager_state *state) {
    char text[NBT_MANAGER_STATE_MAX];
    const int length = nbt_manager_state_format(state, text, sizeof(text));
    if (length < 0) {
        errno = EINVAL;
        return -1;
    }
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_STATE_NEXT, "manager", NULL, NBT_FS_CREATE);
    if (fd < 0) {
        return -1;
    }
    stateFault("created");
    if (writeAll(fd, text, static_cast<size_t>(length))) {
        const int saved = errno;
        close(fd);
        errno = saved ? saved : EIO;
        return -1;
    }
    stateFault("written");
    if (fsync(fd)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    stateFault("synced");
    if (nbt_fs_rename(fs, NBT_FS_MANAGER_STATE_NEXT, NBT_FS_MANAGER_STATE, "manager", NULL)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    stateFault("renamed");
    if (fsync(fd) || managerBarrier(fs)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    stateFault("barrier");
    return close(fd);
}

} // namespace launcher
} // namespace nbt
