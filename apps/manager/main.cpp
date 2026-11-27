// SPDX-License-Identifier: MPL-2.0
/* Manager GUI executable entrypoint, single-instance lock and process setup. */
#include "nbt/capture/protocol.h"
#include "nbt/controller_config.h"
#include "nbt/core/crash.hpp"
#include "nbt/core/system.hpp"
#include "nbt/fs.h"
#include "nbt/gui/car_language.hpp"
#include "nbt/gui/gui_session.hpp"
#include "nbt/gui/hmi_hold.hpp"
#include "nbt/log.h"
#include "nbt/manager_supervisor.h"
#include "nbt/most_input.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#define MANAGER_ROOT "/var/opt/nbt-manager"
#ifndef NBT_MANAGER_VERSION
#define NBT_MANAGER_VERSION "dev"
#endif
// The car's Wi-Fi Direct group: the car starts it at boot (Miracast coded), unlike
// the CarPlay hotspot uap0, which a phone joins only through CarPlay or Android Auto.
#define SESSION_INTERFACE "wfd0"
#define SESSION_PORT 8080u

static volatile sig_atomic_t stopping;
static void on_signal(int number) {
    (void)number;
    stopping = 1;
}

static int parse_fd(const char *text, int *out) {
    char *end = NULL;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (errno || !end || *end || value < 3 || value > INT_MAX) {
        return -1;
    }
    *out = static_cast<int>(value);
    return 0;
}

static int parse_args(int argc, char **argv, const char **root, const char **testWorker, int *slot,
                      int *readyFd, int *leaseFd, int *workerFd) {
    int argument = 1;
    *root = MANAGER_ROOT;
    *testWorker = NULL;
    *slot = -1;
    *readyFd = -1;
    *leaseFd = -1;
    *workerFd = -1;
#ifdef NBT_TESTING
    if (argc >= 5 && !strcmp(argv[1], "--test-root") && !strcmp(argv[3], "--test-worker")) {
        *root = argv[2];
        *testWorker = argv[4];
        argument = 5;
    }
#endif
    if (argc >= argument + 2 && !strcmp(argv[argument], "--slot")) {
        if (strcmp(argv[argument + 1], "0") && strcmp(argv[argument + 1], "1")) {
            return -1;
        }
        *slot = argv[argument + 1][0] - '0';
        argument += 2;
    }
    if (argc >= argument + 4 && !strcmp(argv[argument], "--ready-fd") &&
        !strcmp(argv[argument + 2], "--lease-fd")) {
        if (parse_fd(argv[argument + 1], readyFd) || parse_fd(argv[argument + 3], leaseFd) ||
            *readyFd == *leaseFd) {
            return -1;
        }
        argument += 4;
    }
    // The launcher's channel to the worker it started (PROTOCOL.md).
    if (argc == argument + 2 && !strcmp(argv[argument], "--worker-fd")) {
        return parse_fd(argv[argument + 1], workerFd) || *workerFd == *readyFd ||
                       *workerFd == *leaseFd
                   ? -1
                   : 0;
    }
    return argc == argument ? 0 : -1;
}

/* Failures before the debug log is open go to standard error, which the
 * launcher passes on (the debug log while it is on). */
static int early_failure(const char *what) {
    fprintf(stderr, "nbt-manager-gui: %s (%s)\n", what, strerror(errno));
    return 1;
}

static int install_signals() {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) {
        return -1;
    }
    action.sa_handler = SIG_IGN;
    return sigaction(SIGPIPE, &action, NULL) ? -1 : 0;
}

static int acquire_update_lease(int readyFd, int leaseFd) {
    if (readyFd < 0) {
        return 0;
    }
    struct stat st;
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_SET;
    if (fstat(leaseFd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        fcntl(leaseFd, F_SETLK, &lock) || fcntl(leaseFd, F_SETFD, FD_CLOEXEC) ||
        fcntl(readyFd, F_SETFD, FD_CLOEXEC)) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *root = NULL, *testWorker = NULL;
    int readyFd = -1, leaseFd = -1, slot = -1, workerFd = -1;
    if (parse_args(argc, argv, &root, &testWorker, &slot, &readyFd, &leaseFd, &workerFd)) {
        fprintf(stderr, "nbt-manager-gui: invalid arguments\n");
        return 2;
    }

    char markerPath[PATH_MAX], debugPath[PATH_MAX];
    if (snprintf(markerPath, sizeof(markerPath), "%s/manager/crash.gui", root) < 0 ||
        snprintf(debugPath, sizeof(debugPath), "%s/manager/debug.log", root) < 0 ||
        strlen(root) + sizeof("/manager/debug.log") > sizeof(markerPath)) {
        return early_failure("manager root path too long");
    }
    const bool recovered = !access(markerPath, F_OK) && unlink(markerPath) == 0;
    const nbt::core::CrashConfig crashConfig = {"gui", markerPath, debugPath};
    if (!nbt::core::installCrashHandlers(crashConfig).ok()) {
        return early_failure("crash handlers");
    }

    nbt::gui::GuiSessionConfig config = {root,
                                         NBT_MANAGER_VERSION,
                                         SESSION_INTERFACE,
                                         SESSION_PORT,
                                         slot,
                                         readyFd,
                                         workerFd,
                                         testWorker,
                                         nbt::gui::kHoldSignalPath,
                                         nbt::gui::kHoldLivenessPath,
                                         NBT_CAPTURE_CONTROL_PATH,
                                         NBT_CAPTURE_DIAG_PATH,
                                         recovered,
                                         nbt::gui::kCarLanguagePath,
                                         nbt::model::kCarMakePath,
                                         false};
    const char *inputLibrary = NULL;
#ifdef NBT_TESTING
    char testLivenessPath[PATH_MAX];
    char testCapturePath[PATH_MAX];
    if (testWorker) {
        config.interfaceName = "lo";
        config.port = 0;
        if (snprintf(testLivenessPath, sizeof(testLivenessPath), "%s/gui-live", root) < 0 ||
            strlen(root) + sizeof("/gui-live") > sizeof(testLivenessPath) ||
            snprintf(testCapturePath, sizeof(testCapturePath), "%s/capture-ctrl", root) < 0 ||
            strlen(root) + sizeof("/capture-ctrl") > sizeof(testCapturePath)) {
            return 1;
        }
        config.livenessPath = testLivenessPath;
        config.capturePath = testCapturePath;
    }
    if (getenv("NBT_TEST_HMI_SIGNAL")) {
        config.signalPath = getenv("NBT_TEST_HMI_SIGNAL");
    }
    if (getenv("NBT_TEST_GUI_LIVENESS")) {
        config.livenessPath = getenv("NBT_TEST_GUI_LIVENESS");
        if (!testWorker && snprintf(testCapturePath, sizeof(testCapturePath), "%s.capture",
                                    config.livenessPath) > 0) {
            config.capturePath = testCapturePath;
        }
    }
    if (getenv("NBT_TEST_CAPTURE_CONTROL")) {
        config.capturePath = getenv("NBT_TEST_CAPTURE_CONTROL");
    }
    config.diagPath = getenv("NBT_TEST_CAPTURE_DIAG"); // unset: not watched
    if (testWorker || getenv("NBT_TEST_CAR_LANGUAGE")) {
        config.languagePath = getenv("NBT_TEST_CAR_LANGUAGE"); // unset: none
        config.makePath = getenv("NBT_TEST_CAR_MAKE");
    }
    inputLibrary = getenv("NBT_TEST_INPUT_LIBRARY");
#endif
    /* Started right after a GUI that ended for a manager update: the launcher
     * holds iDrive's liveness line until this one writes its own, at once. */
    config.handover = readyFd >= 0 && getenv(NBT_MANAGER_HANDOVER_ENV);
    (void)unsetenv(NBT_MANAGER_HANDOVER_ENV);
    if (config.handover) {
        const nbt::core::Result<uint64_t> startMs = nbt::core::monotonicMs();
        if (startMs.get()) {
            (void)nbt::gui::holdLivenessWrite(config.livenessPath, getpid(), startMs.value());
        }
    }

    nbt::gui::GuiSession session;
    if (!nbt::core::childReapingInit().ok() || install_signals()) {
        return early_failure("process setup");
    }
    if (session.init(config)) {
        return early_failure("GUI setup");
    }

    nbt_fs fs;
    char error[160];
    int lockfd = -1, result = 1;
    const char *overrideReason = NULL;
    struct flock lock;
    bool workerStarted = false, workerTaken = false;
    int inputFd = -1;
    nbt_ccs_config controller;
    int configStatus = 0;

    if (acquire_update_lease(readyFd, leaseFd)) {
        early_failure("launcher lease");
        goto done;
    }
    if (nbt_fs_init(&fs, root, error, sizeof(error))) {
        fprintf(stderr, "nbt-manager-gui: manager root: %s\n", error);
        goto done;
    }
    if (nbt_log_open(&fs, "gui") > 0) {
        nbt_logf("start slot %d%s", slot, readyFd >= 0 ? " (health check)" : "");
        if (recovered) {
            nbt_logf("recovered from a fault");
        }
    }
    lockfd = nbt_fs_open(&fs, NBT_FS_MANAGER_LOCK, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (lockfd < 0) {
        nbt_logf("manager lock: errno %d", errno);
        goto done;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_len = 1; // byte 0: the launcher may hold byte 1 (src/launcher/update_handover.cpp)
    if (fcntl(lockfd, F_SETLK, &lock)) {
        if (errno == EACCES || errno == EAGAIN) {
            result = 0;
            overrideReason = "already running";
        }
        goto done;
    }
    workerTaken = true;
    configStatus = session.start(workerStarted);
    if (configStatus) {
        result = configStatus > 0 ? 0 : 1; // 0: ended without a restart
        goto done;
    }
    configStatus = nbt_controller_config_load(&fs, &controller);
    if (configStatus < 0) {
        session.setNotice(nbt::gui::text(nbt::gui::T_CONTROLLER_CONFIG_INVALID));
    }
    nbt_logf("controller config %s", configStatus > 0 ? "absent; defaults"
                                     : configStatus   ? "invalid; defaults"
                                                      : "applied");
    session.setCaptureController(controller);
    if (workerStarted) {
        inputFd = nbt_most_input_start(inputLibrary, &controller);
    }
    if (inputFd >= FD_SETSIZE) {
        close(inputFd);
        inputFd = -1;
    }
    nbt_logf(inputFd >= 0 ? "controller input started" : "controller input unavailable; preview");
    result = session.run(inputFd, stopping);
    if (!result && session.updating()) {
        result = NBT_MANAGER_GUI_EXIT_UPDATE;
    }

done:
    switch (nbt_most_input_stop(300)) {
    case 1:
        nbt_logf("controller release done");
        break;
    case -1:
        nbt_logf("controller release timed out");
        break;
    default:
        break;
    }
    session.closeSession(); // logs whether iDrive got the panel back
    nbt_logf("exit (%s), status %d", overrideReason ? overrideReason : session.exitReason(),
             result);
    nbt_log_close();
    if (session.updating()) {
        session.handLivenessToLauncher(); // fresh again after the slow steps above
    } else {
        (void)unlink(config.livenessPath);
    }
    if (config.capturePath) {
        (void)unlink(config.capturePath);
    }
    if (lockfd >= 0) {
        close(lockfd);
    }
    if (readyFd >= 0) {
        close(readyFd);
    }
    if (!workerTaken && workerFd >= 0) { // the session never took the channel
        close(workerFd);
    }
    if (leaseFd >= 0) {
        close(leaseFd);
    }
    fflush(NULL);
#ifdef __QNX__
    /* nbt_most_input_start opens libsys_dsi_colibry with a detached DSI thread;
     * avoid exit()'s shared-library teardown racing it on QNX. */
    _exit(result);
#else
    return result;
#endif
}
