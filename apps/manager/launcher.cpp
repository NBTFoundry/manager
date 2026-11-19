#include "nbt/app_provision.h"
#include "nbt/fs.h"
#include "nbt/launcher/capture_selftest.hpp"
#include "nbt/launcher/door.hpp"
#include "nbt/launcher/hmi_restart.hpp"
#include "nbt/launcher/shim_trial.hpp"
#include "nbt/log.h"
#include "nbt/manager_bootstrap.h"
#include "nbt/manager_receive.h"
#include "nbt/manager_refresh.h"
#include "nbt/manager_supervisor.h"
#include "nbt/storage/capture_install.hpp"
#include "nbt/util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

/* iDrive's plugin folder and starter.cfg; 1 when a test left them unset. */
int capturePaths(const char **hmi, const char **starterCfg) {
    *hmi = "/opt/hmi/ID5/data/ro";
    *starterCfg = "/opt/sys/etc/starter.cfg";
#ifdef NBT_TESTING
    *hmi = getenv("NBT_TEST_HMI_ROOT");
    *starterCfg = getenv("NBT_TEST_STARTER_CFG");
    if (!*hmi) {
        return 1;
    }
#endif
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    const char *root = "/var/opt/nbt-manager";
    nbt_fs fs;
    char error[160];
    int bootstrap = argc == 6 && !strcmp(argv[1], "--bootstrap-manager");
    uint64_t key_size = 0;
    int transfer =
        argc == 4 && (!strcmp(argv[1], "--stage-update") || !strcmp(argv[1], "--stage-recovery"));
    uint64_t size = 0;
    unsigned char digest[32];
    int provision = argc == 7 && !strcmp(argv[1], "--provision-app");
    int refresh = argc == 2 && !strcmp(argv[1], "--refresh-setup");
    int removeCapture = argc == 2 && !strcmp(argv[1], "--remove-capture");
    int capabilities = argc == 2 && !strcmp(argv[1], "--capabilities");
    /* The capture shim, kept automatically (docs/hmi-restart.md): --shim-check
     * (the plugin, 60 s after iDrive started), --shim on|off, --try-shim. */
    int tryShim = argc == 2 && !strcmp(argv[1], "--try-shim");
    int shimCheck = argc == 2 && !strcmp(argv[1], "--shim-check");
    int shimSwitch = argc == 3 && !strcmp(argv[1], "--shim") &&
                     (!strcmp(argv[2], "on") || !strcmp(argv[2], "off"));
    if (capabilities) {
        printf("formats=NBTRLS1,NBTRLS2\n");
        return 0;
    }
    /* --restart-hmi: iDrive's program alone, through the process manager
     * (docs/hmi-restart.md); the installer's experimental test tool. */
    if (argc == 2 && !strcmp(argv[1], "--restart-hmi")) {
        return nbt::launcher::restartHmi(stdout);
    }
    /* --capture-selftest <shim> <dir>: the install's live test of the capture
     * shim (src/launcher/capture_selftest.cpp), run in a child process. */
    if (argc == 4 && !strcmp(argv[1], "--capture-selftest")) {
        char detail[160];
        bool observer = false;
        if (nbt::launcher::captureSelfTest(argv[2], argv[3], detail, sizeof(detail), &observer)) {
            printf("CAPTURE_SELFTEST FAILED %s\n", detail); // read by the install
            return 1;
        }
        printf("CAPTURE_SELFTEST OK %s\n", observer ? "observer" : "active");
        return 0;
    }
    if (nbt_child_reaping_init()) {
        return 1;
    }
    if (!(argc == 2 && !strcmp(argv[1], "--open")) && !(argc == 2 && !strcmp(argv[1], "--door")) &&
        !transfer && !bootstrap && !provision && !refresh && !removeCapture && !tryShim &&
        !shimCheck && !shimSwitch) {
        return 2;
    }
    /* --provision-app <app> <compat> <key-size> <package-size> <package-sha256> */
    if (provision &&
        (nbt_parse_u64(argv[4], &key_size, 0) || !key_size || nbt_parse_u64(argv[5], &size, 0) ||
         !size || nbt_hex_decode(argv[6], digest, sizeof(digest)))) {
        return 2;
    }
    if (transfer && (nbt_parse_u64(argv[2], &size, 0) || !size ||
                     nbt_hex_decode(argv[3], digest, sizeof(digest)))) {
        return 2;
    }
    if (bootstrap &&
        (nbt_parse_u64(argv[2], &key_size, 0) || !key_size || nbt_parse_u64(argv[3], &size, 0) ||
         !size || nbt_hex_decode(argv[5], digest, sizeof(digest)))) {
        return 2;
    }
#ifdef NBT_TESTING
    root = getenv("NBT_TEST_MANAGER_ROOT");
    if (!root) {
        return 1;
    }
#endif
    if (bootstrap) {
        if (nbt_manager_bootstrap(root, 0, key_size, size, argv[4], digest, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("INSTALLED\n");
        return 0;
    }
    if (provision) {
        if (nbt_app_provision(root, 0, argv[2], argv[3], key_size, size, digest, error,
                              sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("PROVISIONED\n");
        return 0;
    }
    if (nbt_fs_init(&fs, root, error, sizeof(error))) {
        fprintf(stderr, "nbt-manager-launcher: %s\n", error);
        return 1;
    }
    if (tryShim || shimCheck || shimSwitch) {
        const char *hmi = NULL, *starterCfg = NULL;
        if (capturePaths(&hmi, &starterCfg) || !starterCfg) {
            return 1;
        }
        (void)nbt_log_open(&fs, "launcher");
        if (shimCheck) { // no operation lock: an open manager must not cost a strike
            return nbt::launcher::shimCheck(&fs, starterCfg, stdout);
        }
        const int lock = nbt_fs_manager_operation_lock(&fs);
        if (lock < 0) {
            fprintf(stderr, "cannot lock manager operations\n");
            return 1;
        }
        const int rc =
            tryShim ? nbt::launcher::tryShim(&fs, starterCfg, stdout)
                    : nbt::launcher::shimSwitch(&fs, starterCfg, !strcmp(argv[2], "on"), stdout);
        close(lock);
        return rc;
    }
    if (removeCapture) {
        const char *hmi = NULL, *starterCfg = NULL;
        if (capturePaths(&hmi, &starterCfg)) {
            return 1;
        }
        const int lock = nbt_fs_manager_operation_lock(&fs);
        if (lock < 0) {
            fprintf(stderr, "cannot lock manager operations\n");
            return 1;
        }
        int rc = 0;
        if (starterCfg) {
            rc = nbt_capture_remove_installed(&fs, hmi, starterCfg, NULL, error, sizeof(error));
        } else {
            const int tx = nbt_fs_exists(&fs, NBT_FS_CAPTURE_TX, "manager", NULL);
            const int orig = nbt_fs_exists(&fs, NBT_FS_CAPTURE_STARTER_ORIG, "manager", NULL);
            if (tx != 0 || orig != 0) {
                snprintf(error, sizeof(error), "capture removal requires starter.cfg path");
                rc = -1;
            }
        }
        close(lock);
        if (rc) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("CAPTURE_REMOVED\n");
        return 0;
    }
    if (refresh) { /* the installer's Update, after --stage-update (manager_refresh.h) */
        const char *hmi = NULL, *starterCfg = NULL;
        unsigned changed = 0;
        if (capturePaths(&hmi, &starterCfg)) {
            return 1;
        }
        (void)nbt_log_open(&fs, "launcher");
        if (nbt_manager_refresh_staged(&fs, hmi, &changed, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        nbt_logf("installer update: launcher %s, plugin %s, capture %s",
                 changed & NBT_REFRESH_LAUNCHER ? "replaced" : "current",
                 changed & NBT_REFRESH_PLUGIN ? "replaced" : "current",
                 changed & NBT_REFRESH_CAPTURE_REFUSED ? "refused"
                 : changed & NBT_REFRESH_CAPTURE       ? "replaced"
                                                       : "current");
        if (changed & NBT_REFRESH_CAPTURE_REFUSED) {
            fprintf(stderr, "%s\n", error); // "capture shim refused, ...", read by the installer
        }
        /* "plugin": iDrive loads the replaced entry or preload only after a restart. */
        printf(changed & (NBT_REFRESH_PLUGIN | NBT_REFRESH_CAPTURE) ? "REFRESHED plugin\n"
                                                                    : "REFRESHED\n");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--door")) {
        return nbt::launcher::startDoor(&fs);
    }
    if (transfer) {
        if (nbt_manager_receive_update(&fs, 0, size, digest, !strcmp(argv[1], "--stage-recovery"),
                                       error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        if (*error) {
            fprintf(stderr, "%s\n", error);
        }
        printf("PENDING\n");
        return 0;
    }
    return nbt_manager_supervise(&fs) ? 1 : 0;
}
