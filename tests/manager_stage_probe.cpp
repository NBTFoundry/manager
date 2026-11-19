// SPDX-License-Identifier: MPL-2.0
#include "nbt/manager_stage.h"
#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    nbt_fs fs;
    char error[160];
    int initial, recovery, upload;
    unsigned char digest[32], bytes[8192];
    SHA256_CTX sha;
    ssize_t count;
    if (argc != 3 || nbt_fs_init(&fs, argv[1], error, sizeof(error))) {
        return 2;
    }
    if (!strcmp(argv[2], "requested")) { /* the launcher's step at an opening */
        const int staged = nbt_manager_stage_requested(&fs);
        printf("%d\n", staged);
        return staged < 0 ? 1 : 0;
    }
    initial = !strcmp(argv[2], "initial");
    recovery = !strcmp(argv[2], "recovery");
    if (!initial && !recovery && strcmp(argv[2], "update")) {
        return 2;
    }
    upload = nbt_fs_open(&fs, NBT_FS_MANAGER_UPLOAD, "manager", NULL, NBT_FS_READ);
    if (upload < 0) {
        return 2;
    }
    SHA256_Init(&sha);
    while ((count = read(upload, bytes, sizeof(bytes))) > 0) {
        SHA256_Update(&sha, bytes, (size_t)count);
    }
    if (count < 0 || close(upload)) {
        return 2;
    }
    SHA256_Final(digest, &sha);
    if (nbt_manager_stage(&fs, initial, recovery, digest, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (*error) {
        fprintf(stderr, "%s\n", error);
    }
    return 0;
}
