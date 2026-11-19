#include "nbt/app_host.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

void nbt_app_environment_remove(nbt_app_environment *environment, const char *entry) {
    size_t to = 0;
    for (size_t from = 0; environment->list[from]; ++from) {
        if (strcmp(environment->list[from], entry)) {
            environment->list[to++] = environment->list[from];
        }
    }
    environment->list[to] = NULL;
}

void nbt_app_output_to_log(nbt_fs *fs) {
    // The log file exists only while it is on (Test tools).
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_LOG, "manager", NULL, NBT_FS_APPEND);
    if (fd < 0) {
        return;
    }
    (void)dup2(fd, STDOUT_FILENO);
    (void)dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}
