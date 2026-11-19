// SPDX-License-Identifier: MPL-2.0
#include "nbt/apps_key.h"

#include "nbt/fs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "apps-key.inc"

int nbt_apps_key_fd(void) {
#ifdef NBT_TESTING
    const char *path = getenv("NBT_TEST_APPS_KEY");
    if (path && *path) {
        return open(path, O_RDONLY);
    }
#endif
    const size_t size = sizeof(kAppsKeyPem) - 1;
    if (!size) {
        errno = ENOENT;
        return -1;
    }
    FILE *file = nbt_fs_bootstrap_temp(); // private, unlinked, close-on-exec
    if (!file) {
        return -1;
    }
    const int fd = dup(fileno(file));
    const bool written = fwrite(kAppsKeyPem, 1, size, file) == size && !fflush(file);
    fclose(file);
    if (fd < 0 || !written || fcntl(fd, F_SETFD, FD_CLOEXEC) || lseek(fd, 0, SEEK_SET)) {
        const int saved = errno ? errno : EIO;
        if (fd >= 0) {
            close(fd);
        }
        errno = saved;
        return -1;
    }
    return fd;
}
