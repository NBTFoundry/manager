#include "nbt/log.h"
#include "nbt/log/internal.hpp"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Copied at nbt_log_open, before other threads log; used under the log lock. */
static nbt_fs log_fs;

static int open_append(void) {
    int fd = nbt_fs_open(&log_fs, NBT_FS_MANAGER_LOG, "manager", NULL, NBT_FS_APPEND);
    if (fd >= 0 && nbt_log_checked(fd)) {
        close(fd);
        fd = -1;
    }
    return fd;
}

/* 1 when `fd` is still the file at manager/debug.log, 0 when it was replaced. */
static int current(int fd) {
    char path[NBT_PATH_MAX];
    struct stat a, b;
    if (nbt_fs_path(&log_fs, NBT_FS_MANAGER_LOG, "manager", NULL, path, sizeof(path)) ||
        fstat(fd, &a) || lstat(path, &b)) {
        return 0;
    }
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

/* Called with a full log. The write lock on the full file makes one process
 * rotate; the others find it replaced and follow. */
static int rotate(int fd) {
    struct flock range;
    int next = -1, result;
    memset(&range, 0, sizeof(range));
    range.l_type = F_WRLCK;
    range.l_whence = SEEK_SET;
    do {
        result = fcntl(fd, F_SETLKW, &range);
    } while (result < 0 && errno == EINTR);
    if (result) {
        return -1;
    }
    if (current(fd) &&
        !nbt_fs_rename(&log_fs, NBT_FS_MANAGER_LOG, NBT_FS_MANAGER_LOG_OLD, "manager", NULL)) {
        next = nbt_fs_open(&log_fs, NBT_FS_MANAGER_LOG, "manager", NULL, NBT_FS_CREATE);
        if (next >= 0) {
            int flags = fcntl(next, F_GETFL);
            if (flags < 0 || fcntl(next, F_SETFL, flags | O_APPEND)) {
                close(next);
                next = -1;
            }
        }
    }
    if (next < 0) {
        next = open_append();
    }
    range.l_type = F_UNLCK;
    (void)fcntl(fd, F_SETLK, &range);
    return next;
}

int nbt_log_open(nbt_fs *fs, const char *component) {
    char path[NBT_PATH_MAX];
    int fd;
    if (!fs) {
        errno = EINVAL;
        return -1;
    }
    if (nbt_log_enabled()) {
        return 1;
    }
    memcpy(&log_fs, fs, sizeof(log_fs));
    fd = open_append();
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (nbt_fs_path(&log_fs, NBT_FS_MANAGER_LOG, "manager", NULL, path, sizeof(path))) {
        close(fd);
        return -1;
    }
    return nbt_log_attach(component, path, fd, rotate);
}
