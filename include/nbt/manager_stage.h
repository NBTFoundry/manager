// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MANAGER_STAGE_H
#define NBT_MANAGER_STAGE_H

#include "nbt/fs.h"
#include "nbt/manager_state.h"

/* The upload, manager key, profile and slot directories are provisioned locally.
 * The caller owns the descriptors used to populate the upload. A successful
 * update records a pending candidate; the launcher must supervise and confirm
 * it or restore the previous slot before executing any manager component. */
int nbt_manager_stage(nbt_fs *fs, int initial, int recovery, const unsigned char expected_hash[32],
                      char *error, unsigned error_size);
/* Drops a candidate that never confirmed (staged but never opened, or a trial
 * cut short) for the known-good slot, as the launcher does at its next start,
 * so a newer release can be staged over it. The caller holds the manager
 * operation lock: no launcher is running the candidate. Returns 1 when one
 * was dropped, 0 when none is pending, -1 when the known-good slot fails
 * verification or the state cannot be changed. */
int nbt_manager_discard_candidate(nbt_fs *fs);
/* Requires the manager operation lock; reads authenticated active metadata. */
int nbt_manager_active_bridge(nbt_fs *fs, nbt_bridge_provider *out);
int nbt_manager_verify_slot(nbt_fs *fs, const nbt_manager_slot *slot);
int nbt_manager_verify_release(nbt_fs *fs, int fd, const unsigned char expected_hash[32]);
/* At an opening: stages the release a paired phone sent and the driver
 * approved (state/manager/stage.request). 1 staged, 0 none, -1 failed (logged);
 * the request is removed either way. */
int nbt_manager_stage_requested(nbt_fs *fs);

#endif
