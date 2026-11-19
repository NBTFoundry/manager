// SPDX-License-Identifier: MPL-2.0
/* iDrive's environment file and the shim trial record (docs/hmi-restart.md).
 *
 * starter.cfg names state/manager/hmi.env as HmiMain3D's <EnvFile> once the
 * shim is installed. The process manager reads that file each time it starts
 * iDrive, and starts iDrive as stock when it is missing, so the shim is loaded
 * only while the file exists. */
#ifndef NBT_STORAGE_HMI_ENV_HPP
#define NBT_STORAGE_HMI_ENV_HPP

#include "nbt/fs.h"
#include "nbt/storage/starter_config.hpp"

namespace nbt {
namespace storage {

/* Writes "LD_PRELOAD=<BMW's preloads>:<shim>" whole (renamed into place, then
 * synced), so the process manager never reads half a line. 0 or -1. */
int hmiEnvEnable(nbt_fs *fs);
/* Removes the file and syncs; 0 when it is gone (also when it was missing). */
int hmiEnvDisable(nbt_fs *fs);
/* 1 present, 0 missing, -1 error. */
int hmiEnvPresent(nbt_fs *fs);

/* starter.cfg's capture state as read from the file (StarterInvalid when it
 * cannot be read safely). */
StarterConfigState starterConfigStateAt(const char *starterCfg);
/* The installed shim's SHA-256 (state/manager/libnbt-capture.so). */
int installedShimSha(nbt_fs *fs, unsigned char sha[32]);
/* The shim's automatic state (state/manager/shim-state, docs/hmi-restart.md):
 * armed (the environment file is written for the next start), kept (iDrive
 * ran 60 s with it; armed again each time), failed (starts with the shim did
 * not reach 60 s: off until another shim or a manual "on"), off (turned off by
 * hand). `strikes` counts starts with the shim that did not reach 60 s. */
enum ShimState { ShimNone = 0, ShimArmed, ShimKept, ShimFailed, ShimOff };
struct ShimRecord {
    ShimState state;
    unsigned strikes;
    unsigned char sha[32];
};
/* 0 (ShimNone when there is no record) or -1 (unreadable or malformed). */
int shimRecordRead(nbt_fs *fs, ShimRecord &record);
int shimRecordWrite(nbt_fs *fs, const ShimRecord &record);
int shimRecordClear(nbt_fs *fs);
/* Armed with no strikes for this shim, and the environment file written. */
int shimArm(nbt_fs *fs, const unsigned char sha[32]);

} // namespace storage
} // namespace nbt

#endif
