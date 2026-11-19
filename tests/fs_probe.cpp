// SPDX-License-Identifier: MPL-2.0
#include "nbt/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    nbt_fs fs;
    char error[128], path[NBT_PATH_MAX];
    int fd, result;
    if (argc != 5 || nbt_fs_init(&fs, argv[1], error, sizeof(error))) {
        return 2;
    }
    if (!strcmp(argv[2], "setup-layout")) {
        return nbt_fs_setup_layout(&fs) ? 1 : 0;
    }
    if (!strcmp(argv[2], "setup-key")) {
        fd = nbt_fs_setup_create(&fs, NBT_FS_SETUP_KEY_NEXT);
        if (fd < 0) {
            return 1;
        }
        result = write(fd, "key", 3) != 3;
        if (close(fd)) {
            return 1;
        }
        if (result || nbt_fs_setup_publish(&fs, NBT_FS_SETUP_KEY_NEXT)) {
            return 1;
        }
        return 0;
    }
    if (!strcmp(argv[2], "setup-forbidden")) {
        return nbt_fs_setup_create(&fs, NBT_FS_ACTIVE) < 0 &&
                       nbt_fs_open(&fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_CREATE) < 0
                   ? 0
                   : 1;
    }
    if (!strcmp(argv[2], "create")) {
        fd = nbt_fs_open(&fs, NBT_FS_UPLOAD, argv[3], NULL, NBT_FS_CREATE);
        if (fd < 0) {
            return 1;
        }
        result = write(fd, "safe", 4) != 4;
        close(fd);
        return result;
    }
    if (!strcmp(argv[2], "read")) {
        fd = nbt_fs_open(&fs, NBT_FS_ACTIVE, argv[3], NULL, NBT_FS_READ);
        if (fd < 0) {
            return 1;
        }
        close(fd);
        return 0;
    }
    if (!strcmp(argv[2], "path")) {
        return nbt_fs_path(&fs, NBT_FS_ACTIVE, argv[3], NULL, path, sizeof(path)) ? 1 : 0;
    }
    if (!strcmp(argv[2], "tx")) {
        return nbt_fs_create_tx(&fs, argv[3], argv[4]) ? 1 : 0;
    }
    if (!strcmp(argv[2], "forbidden")) {
        return nbt_fs_open(&fs, NBT_FS_ACTIVE, argv[3], NULL, NBT_FS_CREATE) < 0 ? 0 : 1;
    }
    return 2;
}
