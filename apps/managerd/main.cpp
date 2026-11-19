// SPDX-License-Identifier: MPL-2.0
#include "nbt/core/system.hpp"
#include "nbt/process/sandbox_limits.hpp"
#include "nbt/worker/daemon.hpp"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

/* Early failures go to standard error: the launcher passes its own on, the
 * debug log while it is on. */
static int failed(const char *what) {
    fprintf(stderr, "nbt-managerd: %s (%s)\n", what, strerror(errno));
    return 1;
}

static int finishWorker(int status) {
    fflush(NULL);
#ifdef __QNX__
    /* DsiClient starts a PTHREAD_CREATE_DETACHED thread inside libsys_dsi_colibry;
     * exit()'s ldqnx _list_release teardown can SIGSEGV against it on QNX. */
    _exit(status);
#else
    return status;
#endif
}

int main(int argc, char **argv) {
    const char *root = "/var/opt/nbt-manager";
    // Before any setup: what this process changes, the application inherits.
    if (argc >= 2 && !strcmp(argv[1], "--app-exec")) {
        return nbt::process::appExecMain(argc, argv);
    }
    if (!nbt::core::childReapingInit().ok()) {
        return failed("child reaping");
    }
    if (argc >= 2 && !strcmp(argv[1], "--door")) {
        return nbt::worker::doorMain(root, argc, argv);
    }
    if (argc >= 3 && (!strcmp(argv[1], "--host") || !strcmp(argv[1], "--host-auto"))) {
        return finishWorker(nbt::worker::hostMain(root, argc, argv));
    }
    if (argc >= 2 && !strcmp(argv[1], "--autostart")) {
        return nbt::worker::autostartMain(root, argv[0], argc, argv);
    }
    if (argc < 3 || strcmp(argv[1], "--control-fd")) {
        fprintf(stderr, "nbt-managerd: invalid arguments\n");
        return 2;
    }
    errno = 0;
    char *end = NULL;
    const long control = strtol(argv[2], &end, 10);
    if (errno || !end || *end || control < 3 || control >= FD_SETSIZE) {
        fprintf(stderr, "nbt-managerd: invalid control descriptor\n");
        return 2;
    }
#ifdef NBT_TESTING
    if (argc == 5 && !strcmp(argv[3], "--root")) {
        root = argv[4];
    } else if (argc != 3) {
        return 2;
    }
#else
    if (argc != 3) {
        return 2;
    }
#endif
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, NULL)) {
        return failed("signal setup");
    }
    int status = 1;
    {
        nbt::worker::Daemon daemon(static_cast<int>(control));
        const nbt::core::Status initialized = daemon.initialize(root, argv[0]);
        if (!initialized.ok()) {
            fprintf(stderr, "nbt-managerd: %s (%s)\n", initialized.message(),
                    strerror(initialized.code()));
            return 1;
        }
        status = daemon.run();
    }
    return finishWorker(status);
}
