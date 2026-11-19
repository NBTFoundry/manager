// SPDX-License-Identifier: MPL-2.0
/* The capture shim kept automatically: the 60 s check the plugin starts inside
 * iDrive, turning it on or off by hand, and the trial's car operations
 * (docs/hmi-restart.md). */
#include "nbt/launcher/shim_trial.hpp"

#include "nbt/log.h"

#include <errno.h>
#include <string.h>

namespace nbt {
namespace launcher {

using storage::ShimRecord;
using storage::ShimState;

ShimDecision decideShimCheck(const ShimRecord &record, bool loaded, bool envPresent) {
    ShimDecision d = {record.state, record.strikes, 0, NULL};
    switch (record.state) {
    case storage::ShimFailed:
        d.log = "capture: shim off after failed starts (on again: another shim or Shim on)";
        return d;
    case storage::ShimOff:
        d.log = "capture: shim turned off";
        return d;
    case storage::ShimNone:
        d.state = storage::ShimArmed;
        d.strikes = 0;
        d.env = loaded ? 0 : +1;
        d.log = "capture: shim armed for iDrive's next start";
        if (!loaded) {
            return d;
        }
        break; // loaded without a record: judged as below
    default:
        break;
    }
    if (loaded && envPresent) {
        d.state = storage::ShimFailed;
        d.env = -1;
        d.log = "capture: the shim did not delete iDrive's environment file; shim off";
    } else if (loaded) {
        d.state = storage::ShimKept;
        d.strikes = 0;
        d.env = +1;
        d.log = "capture: iDrive has run 60 s with the shim; armed for its next start";
    } else if (envPresent) {
        d.log = "capture: shim armed; iDrive has not started with it yet (after an update the "
                "head unit restarts once first)";
    } else if (record.strikes + 1u >= kShimMaxStrikes) {
        d.state = storage::ShimFailed;
        d.strikes = record.strikes + 1u;
        d.log = "capture: two starts with the shim ended before 60 s; shim off";
    } else {
        d.state = storage::ShimArmed;
        d.strikes = record.strikes + 1u;
        d.env = +1;
        d.log = "capture: a start with the shim ended before 60 s; trying it once more";
    }
    return d;
}

namespace {

/* starter.cfg names the environment file and the shim is installed. */
bool installed(nbt_fs *fs, const char *starterCfg, unsigned char sha[32]) {
    return storage::starterConfigStateAt(starterCfg) == storage::StarterWithShim &&
           !storage::installedShimSha(fs, sha);
}

int errnoOr(int rc) {
    return rc ? (errno ? errno : EIO) : 0;
}

struct CarShim {
    nbt_fs *fs;
    unsigned char sha[32];
};

int envOn(void *c) {
    return errnoOr(storage::hmiEnvEnable(static_cast<CarShim *>(c)->fs));
}
int envOff(void *c) {
    return errnoOr(storage::hmiEnvDisable(static_cast<CarShim *>(c)->fs));
}
int envPresent(void *c) {
    return storage::hmiEnvPresent(static_cast<CarShim *>(c)->fs);
}
bool shimLoaded(void *, pid_t pid) {
    return processHasLibrary(pid, "libnbt-capture.so");
}
int setState(void *c, ShimState state) {
    CarShim &shim = *static_cast<CarShim *>(c);
    ShimRecord record;
    memset(&record, 0, sizeof(record));
    record.state = state;
    memcpy(record.sha, shim.sha, 32);
    return errnoOr(storage::shimRecordWrite(shim.fs, record));
}

} // namespace

int shimCheck(nbt_fs *fs, const char *starterCfg, FILE *out) {
    unsigned char sha[32];
    if (!installed(fs, starterCfg, sha)) {
        fprintf(out, "SHIM not installed\n");
        return 0;
    }
    ShimRecord record;
    if (storage::shimRecordRead(fs, record) || memcmp(record.sha, sha, 32)) {
        memset(&record, 0, sizeof(record)); // unreadable or another shim's
    }
    const pid_t hmi = carRestartOps().findHmi(NULL);
    const bool loaded = hmi > 0 && processHasLibrary(hmi, "libnbt-capture.so");
    const int present = storage::hmiEnvPresent(fs);
    if (present < 0) {
        fprintf(out, "SHIM FAILED cannot check iDrive's environment file\n");
        return 1;
    }
    const ShimDecision d = decideShimCheck(record, loaded, present > 0);
    int rc = 0;
    if (d.env > 0) {
        rc |= storage::hmiEnvEnable(fs);
    } else if (d.env < 0) {
        rc |= storage::hmiEnvDisable(fs);
    }
    if (d.state != record.state || d.strikes != record.strikes) {
        ShimRecord next = record;
        next.state = d.state;
        next.strikes = d.strikes;
        memcpy(next.sha, sha, 32);
        rc |= storage::shimRecordWrite(fs, next);
    }
    nbt_logf("%s%s", d.log, rc ? " (could not be saved)" : "");
    fprintf(out, "SHIM %s\n", d.log);
    return rc ? 1 : 0;
}

int shimSwitch(nbt_fs *fs, const char *starterCfg, bool on, FILE *out) {
    unsigned char sha[32];
    const bool known = installed(fs, starterCfg, sha);
    if (on) {
        if (!known) {
            fprintf(out, "SHIM FAILED no shim installed in iDrive's start: run Update\n");
            return 1;
        }
        if (storage::shimArm(fs, sha)) {
            fprintf(out, "SHIM FAILED the shim could not be armed\n");
            return 1;
        }
        nbt_logf("capture: shim turned on");
        fprintf(out, "SHIM ON armed for iDrive's next start, kept after 60 s of running\n");
        return 0;
    }
    CarShim shim;
    shim.fs = fs;
    memcpy(shim.sha, sha, 32);
    if (storage::hmiEnvDisable(fs) || (known && setState(&shim, storage::ShimOff))) {
        fprintf(out, "SHIM FAILED the shim could not be turned off\n");
        return 1;
    }
    nbt_logf("capture: shim turned off");
    fprintf(out, "SHIM OFF iDrive starts without the shim from its next start\n");
    return 0;
}

int tryShim(nbt_fs *fs, const char *starterCfg, FILE *out) {
    CarShim shim;
    shim.fs = fs;
    if (!installed(fs, starterCfg, shim.sha)) {
        fprintf(out, "SHIM_TRIAL FAILED no shim installed in iDrive's start: run Update first\n");
        return 1;
    }
    ShimTrialOps ops;
    ops.restart = carRestartOps();
    ops.restart.ctx = &shim;
    ops.envOn = &envOn;
    ops.envOff = &envOff;
    ops.envPresent = &envPresent;
    ops.shimLoaded = &shimLoaded;
    ops.setState = &setState;
    const int rc = runShimTrial(ops, out);
    nbt_logf("capture: shim trial %s", rc ? "failed" : "passed");
    return rc;
}

} // namespace launcher
} // namespace nbt
