#ifndef NBT_MANAGER_STORE_H
#define NBT_MANAGER_STORE_H

#include "nbt/manager_state.h"

/* Load and transition calls serialize on the managed manager lock. The caller
 * must verify the signed release and its staged slot before a transition, and
 * re-verify slot content against the loaded record before launching it. */
/* Read-only preliminary inspection; decisions must be rechecked under lock. */
int nbt_manager_store_inspect(nbt_fs *fs, nbt_manager_state *state);
int nbt_manager_store_load(nbt_fs *fs, nbt_manager_state *state);
int nbt_manager_store_initial(nbt_fs *fs, const nbt_release *release);
/* Only after re-verifying every component of the initial slot. */
int nbt_manager_store_resume_initial(nbt_fs *fs, const nbt_release *release);
int nbt_manager_store_begin(nbt_fs *fs, const nbt_release *release, int recovery);
int nbt_manager_store_trial(nbt_fs *fs);
int nbt_manager_store_rollback(nbt_fs *fs);
int nbt_manager_store_confirm(nbt_fs *fs, const nbt_process *candidate, uint64_t now_ms);
/* Records a restart durably before backoff. Returns 1 when the limit is reached. */
int nbt_manager_store_restart(nbt_fs *fs, uint64_t epoch_seconds, unsigned *delay_ms);
int nbt_manager_store_clear_restarts(nbt_fs *fs);

#endif
