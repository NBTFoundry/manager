// SPDX-License-Identifier: MPL-2.0
/* Restarts iDrive's program alone through the process manager's package
 * request, so the installer stays connected (docs/hmi-restart.md). */
#include "nbt/launcher/hmi_restart.hpp"

#include "nbt/core/unique_fd.hpp"
#include "nbt/util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __QNX__
#include <devctl.h>
#include <sys/mman.h>
#include <sys/procfs.h>
#endif

namespace nbt {
namespace launcher {

namespace {

int hook(int (*fn)(void *), void *ctx, const char *what, FILE *out) {
    const int error = fn ? fn(ctx) : 0;
    if (error) {
        fprintf(out, "HMI_RESTART %s failed: errno %d\n", what, error);
        fflush(out);
    }
    return error;
}

} // namespace

int runHmiRestart(const HmiRestartOps &ops, FILE *out, pid_t *started) {
    const pid_t old = ops.findHmi(ops.ctx);
    if (old <= 0) {
        fprintf(out, "HMI_RESTART FAILED HmiMain3D is not running\n");
        return 1;
    }
    fprintf(out, "HMI_RESTART HmiMain3D pid %d; requesting STOP of package %u.%u\n",
            static_cast<int>(old), kHmiDomain, kHmiPackage);
    fflush(out);
    int error = ops.request(ops.ctx, kStarterStop);
    if (error) {
        fprintf(out, "HMI_RESTART FAILED STOP request: errno %d (nothing changed)\n", error);
        return 1;
    }
    const uint64_t stopAt = ops.nowMs(ops.ctx);
    bool stopped = false;
    while (!(stopped = !ops.alive(ops.ctx, old)) && ops.nowMs(ops.ctx) - stopAt < 10000u) {
        ops.sleepMs(ops.ctx, 50);
    }
    const unsigned stopMs = static_cast<unsigned>(ops.nowMs(ops.ctx) - stopAt);
    if (stopped) {
        fprintf(out, "HMI_RESTART stopped after %u ms; requesting RUN\n", stopMs);
    } else {
        fprintf(out, "HMI_RESTART still running after %u ms; requesting RUN anyway\n", stopMs);
    }
    fflush(out);
    const bool hooked = stopped;
    const int before = hooked ? hook(ops.beforeRun, ops.ctx, "preparing the start", out) : 0;
    error = ops.request(ops.ctx, kStarterRun);
    if (error) {
        const int after = hooked ? hook(ops.afterRun, ops.ctx, "cleaning up", out) : 0;
        (void)after;
        fprintf(out, "HMI_RESTART FAILED RUN request: errno %d\n", error);
        return 1;
    }
    if (!stopped) {
        fprintf(out, "HMI_RESTART FAILED HmiMain3D did not stop\n");
        return 1;
    }
    const uint64_t runAt = ops.nowMs(ops.ctx);
    for (;;) {
        const pid_t now = ops.findHmi(ops.ctx);
        const unsigned runMs = static_cast<unsigned>(ops.nowMs(ops.ctx) - runAt);
        if ((now > 0 && now != old) || runMs >= 30000u) {
            const int after = hook(ops.afterRun, ops.ctx, "cleaning up", out);
            if (now <= 0 || now == old) {
                fprintf(out, "HMI_RESTART FAILED HmiMain3D did not start within 30 s\n");
                return 1;
            }
            fprintf(out, "HMI_RESTART %s HmiMain3D pid %d after %u ms\n",
                    before || after ? "FAILED (a step above failed), but" : "OK",
                    static_cast<int>(now), runMs);
            if (started) {
                *started = now;
            }
            return before || after ? 1 : 0;
        }
        ops.sleepMs(ops.ctx, 100);
    }
}

namespace {

#ifdef __QNX__
bool isHmi(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/as", static_cast<int>(pid));
    core::UniqueFd fd(open(path, O_RDONLY));
    union {
        procfs_debuginfo info;
        char space[sizeof(procfs_debuginfo) + 256];
    } map;
    memset(&map, 0, sizeof(map));
    if (!fd.valid() || devctl(fd.get(), DCMD_PROC_MAPDEBUG_BASE, &map, sizeof(map), NULL) != EOK) {
        return false;
    }
    map.space[sizeof(map.space) - 1] = 0;
    const char *base = strrchr(map.info.path, '/');
    return !strcmp(base ? base + 1 : map.info.path, "HmiMain3D"); // as the capture shim checks
}

pid_t findHmi(void *) {
    DIR *proc = opendir("/proc");
    pid_t found = 0;
    for (struct dirent *e = proc ? readdir(proc) : NULL; e && !found; e = readdir(proc)) {
        char *end = NULL;
        const long pid = strtol(e->d_name, &end, 10);
        if (end && !*end && pid > 1 && isHmi(static_cast<pid_t>(pid))) {
            found = static_cast<pid_t>(pid);
        }
    }
    if (proc) {
        closedir(proc);
    }
    return found;
}

int request(void *, uint32_t command) {
    const uint32_t message[3] = {kHmiDomain, kHmiPackage, command};
    core::UniqueFd fd(open("/dev/starter/start", O_RDWR));
    if (!fd.valid()) {
        return errno ? errno : EIO;
    }
    return devctl(fd.get(), 0x800c050f, const_cast<uint32_t *>(message), sizeof(message), NULL);
}
#else
pid_t findHmi(void *) {
    return 0;
}
int request(void *, uint32_t) {
    return ENOSYS;
}
#endif

bool alive(void *, pid_t pid) {
    return kill(pid, 0) == 0 || errno == EPERM;
}

uint64_t nowMs(void *) {
    uint64_t now = 0;
    return nbt_monotonic_ms(&now) ? 0 : now;
}

void sleepMs(void *, unsigned ms) {
    (void)poll(NULL, 0, static_cast<int>(ms));
}

} // namespace

HmiRestartOps carRestartOps() {
    const HmiRestartOps ops = {&findHmi, &alive, &request, &nowMs, &sleepMs, NULL, NULL, NULL};
    return ops;
}

int restartHmi(FILE *out) {
    return runHmiRestart(carRestartOps(), out);
}

#ifdef __QNX__
bool processHasLibrary(pid_t pid, const char *fileName) {
    static procfs_mapinfo maps[2048]; // the launcher is single-threaded
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/as", static_cast<int>(pid));
    core::UniqueFd fd(open(path, O_RDONLY));
    int total = 0;
    if (!fd.valid() || devctl(fd.get(), DCMD_PROC_MAPINFO, maps, sizeof(maps), &total) != EOK ||
        total <= 0) {
        return false;
    }
    const int count = total < 2048 ? total : 2048;
    for (int i = 0; i < count; ++i) {
        if (!(maps[i].flags & MAP_ELF)) {
            continue;
        }
        union {
            procfs_debuginfo info;
            char space[sizeof(procfs_debuginfo) + 256];
        } map;
        memset(&map, 0, sizeof(map));
        map.info.vaddr = maps[i].vaddr;
        if (devctl(fd.get(), DCMD_PROC_MAPDEBUG, &map, sizeof(map), NULL) != EOK) {
            continue;
        }
        map.space[sizeof(map.space) - 1] = 0;
        const char *base = strrchr(map.info.path, '/');
        if (!strcmp(base ? base + 1 : map.info.path, fileName)) {
            return true;
        }
    }
    return false;
}
#else
bool processHasLibrary(pid_t, const char *) {
    return false;
}
#endif

} // namespace launcher
} // namespace nbt
