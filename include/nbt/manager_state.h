#ifndef NBT_MANAGER_STATE_H
#define NBT_MANAGER_STATE_H

#include "nbt/process.h"
#include "nbt/release.h"
#include <stddef.h>
#include <stdint.h>

#define NBT_MANAGER_STATE_MAX 512u

typedef struct {
    unsigned slot;
    uint64_t sequence;
    unsigned char release_hash[32];
} nbt_manager_slot;

typedef struct {
    unsigned pending;
    uint64_t highwater;
    nbt_manager_slot active;
    nbt_manager_slot previous;
    unsigned restarts;
    uint64_t restart_epoch;
} nbt_manager_state;

/* State operations never mutate the filesystem. The release must have been
 * verified against the locally provisioned manager key by the caller. */
int nbt_manager_state_initial(nbt_manager_state *state, const nbt_release *release);
int nbt_manager_state_begin(nbt_manager_state *state, const nbt_release *release, int recovery);
int nbt_manager_state_trial(nbt_manager_state *state);
int nbt_manager_state_rollback(nbt_manager_state *state);
int nbt_manager_state_confirm(nbt_manager_state *state, const nbt_process *process,
                              uint64_t now_ms);
int nbt_manager_state_parse(const void *data, size_t size, nbt_manager_state *state);
int nbt_manager_state_format(const nbt_manager_state *state, char *buffer, size_t size);

#endif
