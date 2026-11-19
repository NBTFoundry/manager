// SPDX-License-Identifier: MPL-2.0
#include "nbt/release.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    nbt_release release;
    char error[160], *end;
    unsigned long index;
    int fd, key, output, result;
    if (argc != 7) {
        return 2;
    }
    index = strtoul(argv[4], &end, 10);
    if (*end || index >= NBT_RELEASE_MAX_COMPONENTS) {
        return 2;
    }
    fd = open(argv[1], O_RDWR);
    key = open(argv[2], O_RDONLY);
    if (fd < 0 || key < 0) {
        return 2;
    }
    if (nbt_release_verify(fd, key, argv[3], &release, error, sizeof(error))) {
        return 2;
    }
    close(key);
    if (!strcmp(argv[6], "tamper")) {
        unsigned char byte;
        off_t position = release.components[index].offset;
        if (lseek(fd, position, SEEK_SET) < 0 || read(fd, &byte, 1) != 1 ||
            lseek(fd, position, SEEK_SET) < 0) {
            return 2;
        }
        byte ^= 1;
        if (write(fd, &byte, 1) != 1) {
            return 2;
        }
    } else if (strcmp(argv[6], "normal") && strcmp(argv[6], "nonempty") &&
               strcmp(argv[6], "special")) {
        return 2;
    }
    output = !strcmp(argv[6], "special") ? open("/dev/null", O_WRONLY)
                                         : open(argv[5], O_RDWR | O_CREAT | O_EXCL, 0600);
    if (output < 0) {
        return 2;
    }
    if (!strcmp(argv[6], "nonempty") && write(output, "preserve", 8) != 8) {
        return 2;
    }
    result =
        nbt_release_copy_component(fd, &release, (unsigned)index, output, error, sizeof(error));
    close(output);
    close(fd);
    return result ? 1 : 0;
}
