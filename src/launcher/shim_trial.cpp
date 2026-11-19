/* One start of iDrive with the capture shim while the installer is connected
 * (docs/hmi-restart.md); the automatic check is in shim_control.cpp. */
#include "nbt/launcher/shim_trial.hpp"

#include <string.h>

namespace nbt {
namespace launcher {

namespace {

/* Stops and runs package HMI again; the environment file is gone by now. */
void restartWithoutShim(const HmiRestartOps &r, FILE *out) {
    fprintf(out, "SHIM_TRIAL restarting iDrive without the shim\n");
    fflush(out);
    const int stop = r.request(r.ctx, kStarterStop);
    r.sleepMs(r.ctx, 1000);
    const int run = r.request(r.ctx, kStarterRun);
    const uint64_t at = r.nowMs(r.ctx);
    pid_t pid = 0;
    while ((pid = r.findHmi(r.ctx)) <= 0 && r.nowMs(r.ctx) - at < 30000u) {
        r.sleepMs(r.ctx, 100);
    }
    if (pid > 0) {
        fprintf(out, "SHIM_TRIAL iDrive back without the shim, pid %d\n", static_cast<int>(pid));
    } else {
        fprintf(out,
                "SHIM_TRIAL iDrive did not come back (STOP errno %d, RUN errno %d): restart "
                "the head unit; it starts without the shim\n",
                stop, run);
    }
}

/* The shim made iDrive fail: off for good (until another shim or "on"). */
void failed(const ShimTrialOps &ops, FILE *out) {
    const int off = ops.envOff(ops.restart.ctx);
    const int state = ops.setState(ops.restart.ctx, storage::ShimFailed);
    if (off || state) {
        fprintf(out, "SHIM_TRIAL could not record the failure (errno %d, %d): turn the shim off\n",
                off, state);
    }
}

} // namespace

int runShimTrial(const ShimTrialOps &ops, FILE *out) {
    const HmiRestartOps &base = ops.restart;
    void *ctx = base.ctx;
    if (base.findHmi(ctx) <= 0) {
        fprintf(out, "SHIM_TRIAL FAILED iDrive is not running (nothing changed)\n");
        return 1;
    }
    HmiRestartOps r = base;
    r.beforeRun = ops.envOn;
    r.afterRun = NULL;
    pid_t pid = 0;
    if (runHmiRestart(r, out, &pid)) {
        if (base.findHmi(ctx) <= 0) {
            failed(ops, out);
            restartWithoutShim(base, out);
        }
        fprintf(out, "SHIM_TRIAL FAILED iDrive's restart did not complete (above)\n");
        return 1;
    }
    const uint64_t startAt = base.nowMs(ctx);
    bool loaded = false;
    while (!(loaded = ops.shimLoaded(ctx, pid)) && base.alive(ctx, pid) &&
           base.nowMs(ctx) - startAt < 5000u) {
        base.sleepMs(ctx, 100);
    }
    if (!loaded && base.alive(ctx, pid)) {
        fprintf(out, "SHIM_TRIAL FAILED iDrive started without the shim: restart the head unit "
                     "once after the update; the shim stays armed for that start\n");
        return 1;
    }
    if (loaded && ops.envPresent(ctx) != 0) {
        failed(ops, out);
        fprintf(out, "SHIM_TRIAL FAILED the shim did not delete iDrive's environment file, so a "
                     "failed start would repeat; it stays off\n");
        return 1;
    }
    if (loaded) {
        fprintf(out, "SHIM_TRIAL iDrive pid %d loaded the shim; watching it for %u s\n",
                static_cast<int>(pid), kShimTrialWatchMs / 1000u);
        fflush(out);
    }
    while (base.alive(ctx, pid) && base.nowMs(ctx) - startAt < kShimTrialWatchMs) {
        base.sleepMs(ctx, 250);
    }
    if (!base.alive(ctx, pid)) {
        fprintf(out, "SHIM_TRIAL iDrive with the shim ended after %u ms\n",
                static_cast<unsigned>(base.nowMs(ctx) - startAt));
        failed(ops, out);
        restartWithoutShim(base, out);
        fprintf(out, "SHIM_TRIAL FAILED iDrive did not keep running with the shim; it stays off\n");
        return 1;
    }
    const int state = ops.setState(ctx, storage::ShimKept);
    const int armed = state ? 0 : ops.envOn(ctx);
    if (state || armed) {
        fprintf(out, "SHIM_TRIAL FAILED the result could not be saved (errno %d, %d)\n", state,
                armed);
        return 1;
    }
    fprintf(out, "SHIM_TRIAL OK iDrive pid %d runs with the shim; it is kept for the next starts\n",
            static_cast<int>(pid));
    return 0;
}

} // namespace launcher
} // namespace nbt
