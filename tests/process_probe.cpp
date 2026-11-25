// SPDX-License-Identifier: MPL-2.0
#include "nbt/fs.h"
#include "nbt/process.h"
#include "nbt/storage/managed_root.hpp"
#include "nbt/worker/install_check_log.hpp"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* An install health check's run of a candidate that writes and then dies, as
 * the worker runs it: its output and how it ended reach the application log. */
static int installCheck(const char *rootPath, const char *app) {
    nbt::storage::ManagedRoot root;
    nbt::worker::InstallCheckLog log;
    char *const environment[] = {NULL};
    struct timespec delay = {0, 100000000};
    if (!root.initialize(rootPath).ok()) {
        return 2;
    }
    const int output = log.begin(const_cast<nbt_fs *>(root.legacy()), app);
    nbt::core::Result<nbt::process::ChildProcess> child =
        nbt::process::ChildProcess::start(root, app, environment, -1, -1, output, 0);
    if (output < 0 || !child.get()) {
        return 1;
    }
    log.started("v2", 2, child.value());
    nbt::core::Result<bool> running = nbt::core::Result<bool>::success(true);
    for (unsigned i = 1; i < 50 && running.get() && running.value(); ++i) {
        nanosleep(&delay, NULL);
        log.drain();
        running = child.value().poll(static_cast<uint64_t>(i) * 100);
    }
    if (child.value().running()) {
        return 1;
    }
    log.failed(child.value(), false);
    log.end();
    return 0;
}

int main(int argc, char **argv) {
    nbt_fs fs;
    nbt_process child;
    struct timespec delay = {0, 100000000};
    char error[128];
    unsigned i;
    const char *mode = argc == 4 ? argv[3] : "stop";
    if ((argc != 3 && argc != 4) || nbt_fs_init(&fs, argv[1], error, sizeof(error))) {
        return 2;
    }
    if (!strcmp(mode, "check")) {
        return installCheck(argv[1], argv[2]);
    }
    memset(&child, 0, sizeof(child));
    child.ready_fd = child.lease_fd = -1;
    int gamepad[2] = {-1, -1};
    char fdEnv[] = "NBT_GAMEPAD_FD=7", launch[] = "NBT_LAUNCH=user",
         caps[] = "NBT_BRIDGE_CAPS=gamepad";
    char *environment[] = {fdEnv, launch, caps, NULL};
    const bool withGamepad = !strcmp(mode, "gamepad");
    if (withGamepad && socketpair(AF_UNIX, SOCK_DGRAM, 0, gamepad)) {
        return 1;
    }
    const nbt_app_channels channels = {-1, -1, gamepad[1], -1, -1, -1, 0};
    if (withGamepad
            ? nbt_process_start_host_channels(&fs, argv[2], environment, &channels, &child, 0)
            : nbt_process_start(&fs, argv[2], &child, 0)) {
        perror("start");
        return 1;
    }
    for (i = 0; i < 40; ++i) {
        int result = nbt_process_poll(&child, (uint64_t)i * 100);
        if (result != 1) {
            nbt_process_stop(&child);
            return 1;
        }
        if (child.ready) {
            break;
        }
        nanosleep(&delay, NULL);
    }
    if (!child.ready) {
        return 1;
    }
    {
        int lease = nbt_fs_lease_exclusive(&fs, argv[2]);
        if (lease >= 0) {
            close(lease);
            nbt_process_stop(&child);
            return 1;
        }
    }
    if (withGamepad) {
        char data[7];
        if (recv(gamepad[0], data, sizeof(data), MSG_DONTWAIT) != 7 || memcmp(data, "gamepad", 7)) {
            return 1;
        }
        close(gamepad[0]);
        close(gamepad[1]);
    }
    if (!strcmp(mode, "stop") || withGamepad) {
        if (nbt_process_stop(&child)) {
            return 1;
        }
    } else {
        /* Non-blocking termination. A stubborn child ignores SIGTERM and must
         * still be running until the grace period expires. */
        int stubborn = !strcmp(mode, "stubborn");
        uint64_t base = 10000;
        int result = 1;
        if (strcmp(mode, "terminate") && !stubborn) {
            return 2;
        }
        if (nbt_process_reap(&child, base) != -1) {
            return 1; /* termination not requested */
        }
        if (nbt_process_terminate(&child, base) || !child.stopping ||
            nbt_process_terminate(&child, base + 1)) {
            return 1;
        }
        for (i = 0; i < 20 && result == 1; ++i) {
            nanosleep(&delay, NULL);
            result = nbt_process_reap(&child, base + 100);
        }
        if (stubborn) {
            if (result != 1 || child.killed || child.pid <= 0) {
                fprintf(stderr, "stubborn exited\n");
                return 1;
            }
            result = nbt_process_reap(&child, base + NBT_PROCESS_STOP_GRACE_MS);
            if (result < 0 || (result == 1 && !child.killed)) {
                return 1;
            }
            for (i = 0; i < 50 && result == 1; ++i) {
                nanosleep(&delay, NULL);
                result = nbt_process_reap(&child, base + NBT_PROCESS_STOP_GRACE_MS + 1);
            }
        }
        if (result != 0 || child.pid != 0 || child.stopping || child.ready_fd != -1 ||
            child.lease_fd != -1 || nbt_process_reap(&child, base) != 0) {
            return 1;
        }
    }
    {
        int lease = nbt_fs_lease_exclusive(&fs, argv[2]);
        if (lease < 0) {
            return 1;
        }
        close(lease);
    }
    return 0;
}
