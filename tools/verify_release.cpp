#include "nbt/core/unique_fd.hpp"
#include "nbt/release.h"
#include "nbt/util.h"
#include <fcntl.h>
#include <stdio.h>

int main(int argc, char **argv) {
    nbt_release release;
    char error[160], hash[65];
    if (argc != 4) {
        fprintf(stderr, "usage: nbt-verify-release RELEASE MANAGER_KEY COMPAT\n");
        return 2;
    }
    nbt::core::UniqueFd fd(open(argv[1], O_RDONLY));
    if (!fd.valid()) {
        perror("release");
        return 1;
    }
    nbt::core::UniqueFd key(open(argv[2], O_RDONLY));
    if (!key.valid()) {
        perror("key");
        return 1;
    }
    if (nbt_release_verify(fd.get(), key.get(), argv[3], &release, error, sizeof(error))) {
        fprintf(stderr, "release verification failed: %s\n", error);
        return 1;
    }
    if (nbt_hex(release.sha256, sizeof(release.sha256), hash, sizeof(hash))) {
        return 1;
    }
    printf("version=%s\nsequence=%llu\nsha256=%s\nformat=NBTRLS%u\ncomponents=%u\n",
           release.version, (unsigned long long)release.sequence, hash,
           (unsigned)release.format_version, (unsigned)release.component_count);
    return 0;
}
