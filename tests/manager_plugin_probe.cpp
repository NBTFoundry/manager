// SPDX-License-Identifier: MPL-2.0
#include "nbt/manager_plugin.h"
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    nbt_hmi_fs hmi;
    char error[160];
    int release_fd, key_fd;
    ssize_t n;
    unsigned char digest[32], bytes[8192];
    SHA256_CTX hash;
    if (argc != 5 || nbt_hmi_init(&hmi, argv[1])) {
        return 2;
    }
    release_fd = open(argv[2], O_RDONLY);
    key_fd = open(argv[3], O_RDONLY);
    if (release_fd < 0 || key_fd < 0) {
        return 2;
    }
    SHA256_Init(&hash);
    while ((n = read(release_fd, bytes, sizeof(bytes))) > 0) {
        SHA256_Update(&hash, bytes, (size_t)n);
    }
    if (n < 0) {
        return 2;
    }
    SHA256_Final(digest, &hash);
    if (nbt_manager_plugin_install(&hmi, release_fd, key_fd, argv[4], digest, error,
                                   sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        close(release_fd);
        close(key_fd);
        return 1;
    }
    if (close(release_fd) || close(key_fd)) {
        return 2;
    }
    return 0;
}
