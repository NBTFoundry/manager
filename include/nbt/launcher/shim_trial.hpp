// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_SHIM_TRIAL_HPP
#define NBT_LAUNCHER_SHIM_TRIAL_HPP

#include "nbt/fs.h"
#include "nbt/launcher/hmi_restart.hpp"
#include "nbt/storage/hmi_env.hpp"

#include <stdio.h>

namespace nbt {
namespace launcher {

/* The capture shim, kept automatically (docs/hmi-restart.md). The shim deletes
 * iDrive's environment file when it loads into iDrive, so each arming covers
 * one start. 60 s after the plugin starts inside iDrive it runs --shim-check,
 * which decides from the record, whether this iDrive has the shim mapped and
 * whether the file is still there. */
static const unsigned kShimMaxStrikes = 2u;

struct ShimDecision {
    storage::ShimState state;
    unsigned strikes;
    int env; // +1 write the environment file, -1 remove it, 0 leave it
    const char *log;
};
ShimDecision decideShimCheck(const storage::ShimRecord &record, bool loaded, bool envPresent);

/* The launcher's --shim-check, --shim on|off and --try-shim, with the car's
 * operations. Each prints one or more lines ("SHIM ...", "SHIM_TRIAL ..."). */
int shimCheck(nbt_fs *fs, const char *starterCfg, FILE *out);
int shimSwitch(nbt_fs *fs, const char *starterCfg, bool on, FILE *out);
int tryShim(nbt_fs *fs, const char *starterCfg, FILE *out);

/* One start of iDrive with the shim, while connected. */
struct ShimTrialOps {
    HmiRestartOps restart;        // its beforeRun becomes envOn; afterRun is not used
    int (*envOn)(void *ctx);      // 0 or an errno, with restart.ctx
    int (*envOff)(void *ctx);     // 0 or an errno; also when the file is missing
    int (*envPresent)(void *ctx); // 1, 0, or -1
    bool (*shimLoaded)(void *ctx, pid_t pid);
    int (*setState)(void *ctx, storage::ShimState state); // 0 or an errno
};

static const unsigned kShimTrialWatchMs = 20000u;

/* Stops iDrive, writes the environment file, runs iDrive, then checks that the
 * new iDrive loaded the shim, deleted the file and still runs 20 s later: then
 * the shim is kept (armed again). If iDrive ends or does not come back, the
 * shim is marked failed and iDrive started again without it. Prints
 * "SHIM_TRIAL ..." lines; the last is "SHIM_TRIAL OK ..." on success. */
int runShimTrial(const ShimTrialOps &ops, FILE *out);

} // namespace launcher
} // namespace nbt

#endif
