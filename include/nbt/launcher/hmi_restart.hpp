// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_HMI_RESTART_HPP
#define NBT_LAUNCHER_HMI_RESTART_HPP

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

namespace nbt {
namespace launcher {

/* The process manager's package request (docs/hmi-restart.md): a devctl
 * 0x800c050f on /dev/starter/start with {domain, package, command}. */
static const uint32_t kStarterRun = 2u, kStarterStop = 3u;
static const uint32_t kHmiDomain = 0u, kHmiPackage = 14u;

struct HmiRestartOps {
    pid_t (*findHmi)(void *ctx);                 /* 0 when not running */
    bool (*alive)(void *ctx, pid_t pid);         /* the stopped pid still exists */
    int (*request)(void *ctx, uint32_t command); /* 0 or an errno */
    uint64_t (*nowMs)(void *ctx);
    void (*sleepMs)(void *ctx, unsigned ms);
    void *ctx;
    /* Optional (NULL: none), 0 or an errno. beforeRun: once HmiMain3D has
     * stopped, just before RUN (RUN is requested even when it fails).
     * afterRun: as soon as the new HmiMain3D exists, or when RUN failed or
     * timed out; never when beforeRun was not called. */
    int (*beforeRun)(void *ctx);
    int (*afterRun)(void *ctx);
};

/* Stops package HMI (HmiMain3D only), waits up to 10 s for it to end, runs it
 * again and waits up to 30 s for a new HmiMain3D. Run is requested even when
 * the stop did not complete, so the package is never left stopped by us.
 * Prints "HMI_RESTART ..." progress lines to `out` as they happen; the last is
 * "HMI_RESTART OK ..." on success. Returns 0 on success, 1 otherwise; on
 * success the new pid goes to `started` when given. A failed hook fails the
 * restart, after iDrive is back. */
int runHmiRestart(const HmiRestartOps &ops, FILE *out, pid_t *started = NULL);

/* The car's operations (QNX; elsewhere HmiMain3D is never found and every
 * request fails with ENOSYS), ctx and hooks NULL. */
HmiRestartOps carRestartOps();
int restartHmi(FILE *out);
/* True when the process has an ELF object of that file name mapped (QNX;
 * false elsewhere). */
bool processHasLibrary(pid_t pid, const char *fileName);

} // namespace launcher
} // namespace nbt

#endif
