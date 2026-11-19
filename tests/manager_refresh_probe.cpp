// SPDX-License-Identifier: MPL-2.0
/* manager-refresh-probe <root> <hmi-root> [rw|ro]: runs the SETUP refresh with
 * a remount that prints each call and fails the named one. Prints "trial",
 * or "changed=<bits>"; exit 1 with the error on stderr. */
#include "nbt/manager_refresh.h"
#include <stdio.h>
#include <string.h>

static const char *failing = "";

static int remount(int writable) {
    printf("remount %s\n", writable ? "rw" : "ro");
    fflush(stdout);
    return !strcmp(failing, writable ? "rw" : "ro") ? -1 : 0;
}

int main(int argc, char **argv) {
    nbt_fs fs;
    char error[160];
    unsigned changed = 0;
    if ((argc != 3 && argc != 4) || nbt_fs_init(&fs, argv[1], error, sizeof(error))) {
        return 2;
    }
    if (argc == 4) {
        failing = argv[3];
    }
    const int result =
        nbt_manager_refresh_installed(&fs, argv[2], remount, &changed, error, sizeof(error));
    if (result < 0) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (result > 0) {
        puts("trial");
    } else {
        printf("changed=%u\n", changed);
    }
    return 0;
}
