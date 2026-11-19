// SPDX-License-Identifier: MPL-2.0
#include "nbt/manager_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    nbt_fs fs;
    nbt_manager_state state;
    nbt_release release;
    char error[128], text[NBT_MANAGER_STATE_MAX], *end;
    unsigned long sequence;
    int length, result;
    if ((argc != 4 && argc != 5) || nbt_fs_init(&fs, argv[1], error, sizeof(error))) {
        return 2;
    }
    sequence = strtoul(argv[3], &end, 10);
    if (*end || !sequence || sequence > 255) {
        return 2;
    }
    memset(&release, 0, sizeof(release));
    memset(release.sha256, (int)sequence, sizeof(release.sha256));
    release.sequence = sequence;
    if (!strcmp(argv[2], "initial")) {
        result = nbt_manager_store_initial(&fs, &release);
    } else if (!strcmp(argv[2], "begin")) {
        result = nbt_manager_store_begin(&fs, &release, 0);
    } else if (!strcmp(argv[2], "recovery")) {
        result = nbt_manager_store_begin(&fs, &release, 1);
    } else if (!strcmp(argv[2], "rollback")) {
        result = nbt_manager_store_rollback(&fs);
    } else if (!strcmp(argv[2], "trial")) {
        result = nbt_manager_store_trial(&fs);
    } else if (!strcmp(argv[2], "restart")) {
        unsigned delay;
        char *epoch_end;
        uint64_t epoch;
        if (argc != 5) {
            return 2;
        }
        epoch = strtoull(argv[4], &epoch_end, 10);
        if (*epoch_end) {
            return 2;
        }
        result = nbt_manager_store_restart(&fs, epoch, &delay);
    } else if (!strcmp(argv[2], "clear-restarts")) {
        if (argc != 4) {
            return 2;
        }
        result = nbt_manager_store_clear_restarts(&fs);
    } else if (!strcmp(argv[2], "load")) {
        result = nbt_manager_store_load(&fs, &state);
        if (!result) {
            length = nbt_manager_state_format(&state, text, sizeof(text));
            if (length < 0 || fwrite(text, 1, (size_t)length, stdout) != (size_t)length) {
                return 2;
            }
        }
    } else {
        return 2;
    }
    return result ? 1 : 0;
}
