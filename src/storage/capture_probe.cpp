// SPDX-License-Identifier: MPL-2.0
/* Loads the capture shim into a short-lived process before iDrive's start
 * depends on it: QNX's loader stops a program before main for a preload that
 * is malformed, lacks a library or leaves a symbol unresolved (x86 VM cases
 * 05-13, docs/input-capture.md), and iDrive would not start. */
#include "capture_probe.hpp"

#include "capture_io.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/process/child_spawn.hpp"
#include "nbt/storage/starter_config.hpp"
#include "starter_env.hpp"

#include <memory>
#include <new>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace nbt {
namespace storage {
namespace capture_io {
namespace {

const unsigned kEnvMax = 256;
const unsigned kProbeTimeoutMs = 5000;

bool isPreload(const char *entry) {
    return !strncmp(entry, "LD_PRELOAD=", 11);
}

/* Set by HmiMain3D's environment or by the test itself: not inherited. */
bool replacedVariable(const char *entry, const HmiEnv *hmi) {
    if (isPreload(entry) || !strncmp(entry, "LD_BIND_NOW=", 12)) {
        return true;
    }
    for (unsigned i = 0; hmi && i < hmi->count; ++i) {
        if (!strncmp(entry, hmi->entries[i].text, hmi->entries[i].nameLength + 1)) {
            return true;
        }
    }
    return false;
}

const char *const kCapabilities[] = {"--capabilities", NULL};

/* Runs `program args...` in this process's environment with HmiMain3D's (when
 * known) over it; 0 when it exits 0, else the reason. Its first output line
 * goes to `out` when given. */
int runOnce(const char *program, const char *const *args, const HmiEnv *hmi, const char *preload,
            bool bindNow, char *detail, size_t detailSize, char *out = NULL, size_t outSize = 0) {
    char preloadVar[2 * NBT_PATH_MAX + 16];
    char *env[kEnvMax + kHmiEnvMax + 3];
    unsigned n = 0;
    for (char **e = environ; e && *e; ++e) {
        if (n == kEnvMax) { // never a silently shortened environment
            snprintf(detail, detailSize, "environment over %u entries", kEnvMax);
            return -1;
        }
        if (!replacedVariable(*e, hmi)) {
            env[n++] = *e;
        }
    }
    for (unsigned i = 0; hmi && i < hmi->count; ++i) {
        if (!isPreload(hmi->entries[i].text)) {
            env[n++] = const_cast<char *>(hmi->entries[i].text);
        }
    }
    if (preload) {
        if (snprintf(preloadVar, sizeof(preloadVar), "LD_PRELOAD=%s", preload) >=
            static_cast<int>(sizeof(preloadVar))) {
            snprintf(detail, detailSize, "preload list too long");
            return -1;
        }
        env[n++] = preloadVar;
    }
    if (bindNow) {
        env[n++] = const_cast<char *>("LD_BIND_NOW=1"); // every symbol resolved at load
    }
    env[n] = NULL;
    core::UniqueFd null(open("/dev/null", O_RDWR));
    if (!null.valid()) {
        snprintf(detail, detailSize, "/dev/null: errno %d", errno);
        return -1;
    }
    int pipeFds[2] = {-1, -1};
    if (out && pipe(pipeFds)) {
        snprintf(detail, detailSize, "pipe: errno %d", errno);
        return -1;
    }
    core::UniqueFd readEnd(pipeFds[0]), writeEnd(pipeFds[1]);
    const int fds[3] = {null.get(), out ? writeEnd.get() : null.get(), null.get()};
    char *argv[6] = {const_cast<char *>(program), NULL, NULL, NULL, NULL, NULL};
    for (unsigned i = 0; args && args[i] && i < 4; ++i) {
        argv[i + 1] = const_cast<char *>(args[i]);
    }
    pid_t pid = -1;
    const int spawned = process::spawnChild(program, argv, env, fds, 3, &pid);
    writeEnd.reset();
    if (spawned) {
        snprintf(detail, detailSize, "cannot start %s: errno %d", program, spawned);
        return -1;
    }
    int status = 0;
    pid_t done = 0;
    for (unsigned waited = 0; (done = waitpid(pid, &status, WNOHANG)) == 0; waited += 20) {
        if (waited >= kProbeTimeoutMs) {
            (void)kill(pid, SIGKILL);
            (void)waitpid(pid, &status, 0);
            snprintf(detail, detailSize, "%s did not exit within 5 s", program);
            return -1;
        }
        (void)poll(NULL, 0, 20);
    }
    if (done < 0) {
        snprintf(detail, detailSize, "waiting for %s: errno %d", program, errno);
        return -1;
    }
    if (out && outSize) {
        const ssize_t got = read(readEnd.get(), out, outSize - 1); // a line, after the exit
        out[got > 0 ? got : 0] = 0;
        char *newline = strchr(out, '\n');
        if (newline) {
            *newline = 0;
        }
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return 0;
    }
    if (WIFEXITED(status)) {
        snprintf(detail, detailSize, "exit status %d", WEXITSTATUS(status));
    } else {
        snprintf(detail, detailSize, "signal %d", WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    }
    return -1;
}

int writeProbeCopy(nbt_fs *fs, const char *data, size_t len, char path[NBT_PATH_MAX]) {
    if (nbt_fs_path(fs, NBT_FS_CAPTURE_PROBE, "manager", NULL, path, NBT_PATH_MAX)) {
        return -1;
    }
    (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_PROBE, "manager", NULL); // a crashed probe's copy
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_CAPTURE_PROBE, "manager", NULL, NBT_FS_CREATE));
    size_t done = 0;
    while (fd.valid() && done < len) {
        const ssize_t n = write(fd.get(), data + done, len - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        done += static_cast<size_t>(n);
    }
    if (!fd.valid() || done != len || fchmod(fd.get(), 0700) || !fd.close().ok()) {
        (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_PROBE, "manager", NULL);
        return -1;
    }
    return 0;
}

} // namespace

int probeCaptureShim(nbt_fs *fs, const char *cfg, size_t cfgLen, const char *data, size_t len,
                     char *detail, size_t detailSize) {
    char program[NBT_PATH_MAX], copy[NBT_PATH_MAX], list[2 * NBT_PATH_MAX], why[96];
    detail[0] = 0;
#ifdef NBT_TESTING
    const char *testProgram = getenv("NBT_TEST_PRELOAD_PROBE");
    if (!testProgram) {
        return 0; // host tests of the transaction itself use fake shim bytes
    }
    if (snprintf(program, sizeof(program), "%s", testProgram) >=
        static_cast<int>(sizeof(program))) {
        snprintf(detail, detailSize, "probe program path too long");
        return -1;
    }
#else
    // The launcher this release installed: its --capabilities only prints.
    if (nbt_fs_path(fs, NBT_FS_SETUP_LAUNCHER, "manager", NULL, program, sizeof(program))) {
        snprintf(detail, detailSize, "launcher path invalid");
        return -1;
    }
#endif
    std::unique_ptr<HmiEnv> parsed(new (std::nothrow) HmiEnv);
    const HmiEnv *hmi =
        parsed && cfg && hmiStarterEnvironment(cfg, cfgLen, *parsed) ? parsed.get() : NULL;
    if (runOnce(program, kCapabilities, hmi, NULL, false, detail, detailSize)) {
        return -1; // the program alone fails: the test would prove nothing
    }
    if (writeProbeCopy(fs, data, len, copy)) {
        snprintf(detail, detailSize, "cannot write the probe copy: errno %d", errno);
        return -1;
    }
    const char *stock = StarterConfig::stockPreload();
    char reason[160];
    const char *mode;
    int rc;
    if (snprintf(list, sizeof(list), "%s:%s", stock, copy) >= static_cast<int>(sizeof(list))) {
        snprintf(reason, sizeof(reason), "preload list too long");
        mode = "with iDrive's preloads";
        rc = -1;
    } else if (!runOnce(program, kCapabilities, hmi, stock, true, why, sizeof(why))) {
        // As iDrive loads it: its list, then the shim, every symbol bound.
        mode = "after iDrive's preloads, all bound";
        rc = runOnce(program, kCapabilities, hmi, list, true, reason, sizeof(reason));
    } else if (!runOnce(program, kCapabilities, hmi, stock, false, why, sizeof(why))) {
        // iDrive's own libraries leave symbols for HmiMain3D: bound on demand.
        mode = "after iDrive's preloads lazily, alone all bound";
        rc = runOnce(program, kCapabilities, hmi, list, false, reason, sizeof(reason)) ||
             runOnce(program, kCapabilities, hmi, copy, true, reason, sizeof(reason));
    } else {
        mode = "alone, all bound (iDrive's preloads do not load here)";
        rc = runOnce(program, kCapabilities, hmi, copy, true, reason, sizeof(reason));
    }
    // The live test: the launcher drives the shim as the GUI and iDrive will
    // (capture on and off, heartbeat, the 5 s escape, a GUI that is gone).
    char result[96] = "";
    const char *selftest = program;
#ifdef NBT_TESTING
    selftest = getenv("NBT_TEST_CAPTURE_SELFTEST"); // unset: fake shim bytes, no live test
#endif
    if (!rc && selftest) {
        // The car: /dev/shmem, as iDrive's own files. On the manager's disk
        // the shim's mapped records never reached the test's reads (FAILED
        // step 2, record 0, 2026-11-18).
        char stateDir[NBT_PATH_MAX] = "/dev/shmem";
        const char *const args[] = {"--capture-selftest", copy, stateDir, NULL};
#ifndef __QNX__ // the host has no /dev/shmem: the manager's state folder
        const bool dirFailed =
            nbt_fs_path(fs, NBT_FS_STATE_DIR, "manager", NULL, stateDir, sizeof(stateDir)) != 0;
#else
        const bool dirFailed = false;
#endif
        if (dirFailed) {
            snprintf(reason, sizeof(reason), "state path invalid");
            rc = -1;
        } else {
            char ran[96];
            rc =
                runOnce(selftest, args, hmi, NULL, false, ran, sizeof(ran), result, sizeof(result));
            if (rc || strncmp(result, "CAPTURE_SELFTEST OK ", 20)) {
                // The launcher's own line ("FAILED step ..."), else how it ended (a crash).
                const bool said = !strncmp(result, "CAPTURE_SELFTEST ", 17);
                snprintf(reason, sizeof(reason), "live test: %s",
                         said ? result + 17 : (rc ? ran : "no answer"));
                rc = -1;
            }
        }
    }
    (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_PROBE, "manager", NULL);
    const char *environment = hmi ? "HmiMain3D env" : "caller env";
    if (rc) {
        snprintf(detail, detailSize, "%s; shim loaded %s, %s", reason, mode, environment);
    } else {
        snprintf(detail, detailSize, "shim loaded %s, %s%s%s", mode, environment,
                 result[0] ? "; live test " : "", result[0] ? result + 17 : "");
    }
    return rc ? -1 : 0;
}

} // namespace capture_io
} // namespace storage
} // namespace nbt
