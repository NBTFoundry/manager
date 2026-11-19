// SPDX-License-Identifier: MPL-2.0
#include "nbt/core/unique_fd.hpp"
#include "nbt/package.h"
#include "nbt/util.h"
#include <fcntl.h>
#include <stdio.h>

int main(int argc, char **argv) {
    nbt_package package;
    char error[128], hash[65];
    if (argc != 5) {
        fprintf(stderr, "usage: nbt-verify PACKAGE PUBLIC_KEY APP_ID COMPATIBILITY\n");
        return 2;
    }
    nbt::core::UniqueFd fd(open(argv[1], O_RDONLY));
    if (!fd.valid()) {
        perror("package");
        return 1;
    }
    nbt::core::UniqueFd key(open(argv[2], O_RDONLY));
    if (!key.valid()) {
        perror("key");
        return 1;
    }
    if (nbt_package_verify(fd.get(), key.get(), argv[3], argv[4], &package, error, sizeof(error))) {
        fprintf(stderr, "verification failed: %s\n", error);
        return 1;
    }
    if (nbt_hex(package.package_hash, 32, hash, sizeof(hash))) {
        return 1;
    }
    printf("app=%s\nversion=%s\nsequence=%llu\npackage_sha256=%s\n", package.app, package.version,
           (unsigned long long)package.sequence, hash);
    {
        char text[NBT_BRIDGE_TEXT_MAX];
        if (nbt_bridge_requirement_format(&package.bridge, "", text, sizeof(text)) < 0) {
            return 1;
        }
        fputs(text, stdout);
    }
    return 0;
}
