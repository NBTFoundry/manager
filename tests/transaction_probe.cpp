#include "nbt/app_settings.h"
#include "nbt/apps_key.h"
#include "nbt/fs.h"
#include "nbt/package.h"
#include "nbt/transaction.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    nbt_fs fs;
    nbt_package p;
    char error[128];
    int lock, lease, fd, key, rc;
    if (argc != 4 || nbt_fs_init(&fs, argv[1], error, sizeof(error))) {
        return 2;
    }
    lock = nbt_tx_lock(&fs, argv[2]);
    if (lock < 0) {
        return 3;
    }
    lease = nbt_fs_lease_exclusive(&fs, argv[2]);
    if (lease < 0) {
        return 3;
    }
    if (!strcmp(argv[3], "env") || !strcmp(argv[3], "candidate-env")) {
        nbt_installed record;
        nbt_bridge_requirement bridge;
        nbt_app_environment env;
        unsigned i;
        if (!strcmp(argv[3], "candidate-env")) {
            rc = nbt_tx_candidate_bridge(&fs, argv[2], &bridge);
        } else {
            rc = nbt_tx_installed(&fs, argv[2], &record);
            bridge = record.bridge;
        }
        if (!rc) {
            rc = nbt_app_environment_build(&fs, argv[2], "check", &bridge, NULL, &env);
        }
        if (!rc) {
            for (i = 0; env.list[i]; ++i) {
                puts(env.list[i]);
            }
        }
    } else if (!strcmp(argv[3], "recover")) {
        rc = nbt_tx_recover(&fs, argv[2], error, sizeof(error));
    } else if (!strcmp(argv[3], "confirm")) {
        rc = nbt_tx_confirm(&fs, argv[2], error, sizeof(error));
    } else if (!strcmp(argv[3], "begin") || !strcmp(argv[3], "downgrade")) {
        fd = nbt_fs_open(&fs, NBT_FS_UPLOAD, argv[2], NULL, NBT_FS_READ);
        key = nbt_apps_key_fd(); // NBT_TEST_APPS_KEY
        if (fd < 0 || key < 0) {
            return 4;
        }
        rc = nbt_package_verify(fd, key, argv[2], "demo-1", &p, error, sizeof(error));
        close(fd);
        close(key);
        if (!rc) {
            unsigned char approved[32];
            memcpy(approved, p.package_hash, sizeof(approved));
            rc = nbt_tx_begin(&fs, argv[2], approved, !strcmp(argv[3], "downgrade"), &p, error,
                              sizeof(error));
        }
    } else {
        return 2;
    }
    close(lock);
    close(lease);
    if (rc) {
        fprintf(stderr, "%s\n", error);
    }
    return rc ? 1 : 0;
}
