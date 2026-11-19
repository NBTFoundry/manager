// SPDX-License-Identifier: MPL-2.0
#include "nbt/app_host.h"

#include "nbt/display/layer_manager.hpp"
#include "nbt/log.h"
#include "nbt/process/host_control.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// The path is the manager's own; the application argument only has to be valid.
static const char kOwner[] = "manager";

int nbt_app_surface_claim(nbt_fs *fs) {
    if (!fs) {
        errno = EINVAL;
        return -1;
    }
    const int fd = nbt_fs_open(fs, NBT_FS_SURFACE_LOCK, kOwner, NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0) {
        return -1;
    }
    if (nbt::process::HostControl::setByteLock(fd, F_WRLCK, 0)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

pid_t nbt_app_surface_owner(nbt_fs *fs) {
    struct flock lock;
    if (!fs) {
        errno = EINVAL;
        return -1;
    }
    const int fd = nbt_fs_open(fs, NBT_FS_SURFACE_LOCK, kOwner, NULL, NBT_FS_READ);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_len = 1;
    const int result = fcntl(fd, F_GETLK, &lock);
    const int saved = errno;
    close(fd);
    if (result) {
        errno = saved;
        return -1;
    }
    if (lock.l_type == F_UNLCK) {
        return 0;
    }
    return lock.l_type == F_WRLCK && lock.l_pid > 0 ? lock.l_pid : -1;
}

int nbt_app_surface_grant(nbt_fs *fs, const char *app, nbt_app_environment *environment, int *fd) {
    size_t count = 0; // the builder ends the list with NULL
    for (; environment->list[count]; ++count) {
    }
    if (count + 2 > sizeof(environment->list) / sizeof(environment->list[0])) {
        return -1;
    }
    *fd = nbt_app_surface_claim(fs);
    if (*fd < 0) {
        nbt_logf("host %s: the application surface is another's (errno %d)", app, errno);
        return 0;
    }
    snprintf(environment->surface, sizeof(environment->surface), "NBT_SURFACE=%d",
             nbt::display::kAppSurface);
    environment->list[count] = environment->surface;
    environment->list[count + 1] = NULL;
    return 0;
}
