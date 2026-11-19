/* The capture shim's file, next to iDrive's environment file in the manager's
 * state folder (docs/hmi-restart.md), and the folder dev91-dev103 used on
 * /fs/sda0 (`<hmi root>/NbtCapture`). */
#include "capture_io.hpp"

#include "nbt/core/unique_fd.hpp"
#include "nbt/util.h"

#include <dirent.h>
#include <errno.h>
#include <memory>
#include <new>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace capture_io {

namespace {

const size_t kShimMax = 2u * 1024u * 1024u; // as the release reader allows

/* The whole file, at most kShimMax bytes; -1 when missing or unsafe. */
int readShim(nbt_fs *fs, std::unique_ptr<char[]> &buf, size_t *len) {
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_CAPTURE_SHIM, "manager", NULL, NBT_FS_READ));
    struct stat st;
    if (!fd.valid() || fstat(fd.get(), &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        static_cast<uint64_t>(st.st_size) > kShimMax) {
        return -1;
    }
    buf.reset(new (std::nothrow) char[static_cast<size_t>(st.st_size)]);
    size_t done = 0;
    while (buf && done < static_cast<size_t>(st.st_size)) {
        const ssize_t n = read(fd.get(), buf.get() + done, static_cast<size_t>(st.st_size) - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        done += static_cast<size_t>(n);
    }
    *len = done;
    return buf ? 0 : -1;
}

} // namespace

int writeShimFile(nbt_fs *fs, const char *data, size_t len) {
    (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_SHIM_NEXT, "manager", NULL);
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_CAPTURE_SHIM_NEXT, "manager", NULL, NBT_FS_CREATE));
    size_t done = 0;
    while (fd.valid() && done < len) {
        size_t chunk = len - done;
        if (faultMatch("short-shim-write") && chunk > 64) {
            chunk = 64;
        }
        const ssize_t n = write(fd.get(), data + done, chunk);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0 || faultMatch("short-shim-write")) {
            break;
        }
        done += static_cast<size_t>(n);
    }
    // 0700 as the load test's copy, which the same loader read from here.
    if (!fd.valid() || done != len || fchmod(fd.get(), 0700) || faultMatch("sync-shim") ||
        fsync(fd.get()) || !fd.close().ok()) {
        (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_SHIM_NEXT, "manager", NULL);
        return -1;
    }
    crashFault("before-shim-rename");
    if (nbt_fs_rename(fs, NBT_FS_CAPTURE_SHIM_NEXT, NBT_FS_CAPTURE_SHIM, "manager", NULL)) {
        (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_SHIM_NEXT, "manager", NULL);
        return -1;
    }
    return 0;
}

int shimFileSha(nbt_fs *fs, unsigned char sha[32], size_t *size) {
    std::unique_ptr<char[]> buf;
    size_t len = 0;
    if (readShim(fs, buf, &len)) {
        return -1;
    }
    sha256Bytes(buf.get(), len, sha);
    if (size) {
        *size = len;
    }
    return 0;
}

bool shimFileMatches(nbt_fs *fs, uint64_t size, const unsigned char sha[32]) {
    unsigned char got[32];
    size_t len = 0;
    return !shimFileSha(fs, got, &len) && len == size && nbt_constant_equal(got, sha, 32);
}

int removeShimFile(nbt_fs *fs) {
    (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_SHIM_NEXT, "manager", NULL);
    if (nbt_fs_unlink(fs, NBT_FS_CAPTURE_SHIM, "manager", NULL) && errno != ENOENT) {
        return -1;
    }
    return nbt_fs_exists(fs, NBT_FS_CAPTURE_SHIM, "manager", NULL) == 0 ? 0 : -1;
}

int legacyShimDir(const char *capDir, bool allowTemp) {
    struct stat lst;
    if (lstat(capDir, &lst)) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!S_ISDIR(lst.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    DIR *d = opendir(capDir);
    if (!d) {
        return -1;
    }
    for (struct dirent *e = NULL; (e = readdir(d)) != NULL;) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") ||
            !strcmp(e->d_name, "libnbt-capture.so") ||
            (allowTemp && !strcmp(e->d_name, ".libnbt-capture.so.next"))) {
            continue;
        }
        closedir(d);
        errno = EEXIST;
        return -1;
    }
    return closedir(d) ? -1 : 1;
}

int removeLegacyShimDir(const char *capDir) {
    char path[NBT_PATH_MAX];
    const char *const names[] = {".libnbt-capture.so.next", "libnbt-capture.so"};
    for (size_t i = 0; i < 2; ++i) {
        if (snprintf(path, sizeof(path), "%s/%s", capDir, names[i]) >=
                static_cast<int>(sizeof(path)) ||
            (unlink(path) && errno != ENOENT)) {
            return -1;
        }
    }
    struct stat st;
    return (rmdir(capDir) && errno != ENOENT) || !lstat(capDir, &st) ? -1 : 0;
}

} // namespace capture_io
} // namespace storage
} // namespace nbt
