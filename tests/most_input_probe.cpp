// SPDX-License-Identifier: MPL-2.0
/* Starts the MOST input client with a given library and prints received inputs
 * as letters (U, D, S, B, H) until `argv[2]` milliseconds pass without input.
 * With NBT_PROBE_LOG set to an existing file, the debug log goes there. */
#include "nbt/log.h"
#include "nbt/most_input.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>

int main(int argc, char **argv) {
    static const char letters[] = "?UDSBHRO";
    int fd, idle_ms;
    if (argc != 3) {
        return 2;
    }
    idle_ms = atoi(argv[2]);
    if (getenv("NBT_PROBE_LOG") && nbt_log_open_path(getenv("NBT_PROBE_LOG"), "probe") != 1) {
        return 3;
    }
    fd = nbt_most_input_start(argv[1], NULL);
    if (fd < 0) {
        puts("START-FAILED");
        return 1;
    }
    if (nbt_most_input_start(argv[1], NULL) != -1) {
        return 1; /* once per process */
    }
    for (;;) {
        fd_set readers;
        struct timeval wait = {idle_ms / 1000, (idle_ms % 1000) * 1000};
        unsigned char byte;
        FD_ZERO(&readers);
        FD_SET(fd, &readers);
        if (select(fd + 1, &readers, NULL, NULL, &wait) <= 0) {
            break;
        }
        while (read(fd, &byte, 1) == 1) {
            putchar(byte < sizeof(letters) - 1 ? letters[byte] : '!');
        }
    }
    putchar('\n');
    return 0;
}
