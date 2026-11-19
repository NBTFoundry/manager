// SPDX-License-Identifier: MPL-2.0
/* Lockless shared-memory diagnostic ring reader (plugin & tools safe). */
#ifndef NBT_CAPTURE_DIAG_H
#define NBT_CAPTURE_DIAG_H

#include "nbt/capture/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*nbt_capture_diag_sink)(const nbt_capture_diag_slot *slot, void *user);

/* Reads the bounded shared-memory ring snapshot if present and valid.
 * Returns 0 on success, -1 if absent, untrusted, or corrupt. */
int nbt_capture_diag_read(const char *path, nbt_capture_diag_shm *out);

/* Advances *cursor up to snap->write_seq, invoking sink for each valid new
 * slot and setting *missed to any overwritten records skipped. */
void nbt_capture_diag_drain(const nbt_capture_diag_shm *snap, uint32_t *cursor, uint32_t *missed,
                            nbt_capture_diag_sink sink, void *user);

#ifdef __cplusplus
}
#endif
#endif
