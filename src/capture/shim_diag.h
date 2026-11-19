/* The capture shim's diagnostic ring writer (/dev/shmem/nbt-manager-capture-diag),
 * read by the plugin and the GUI (include/nbt/capture/diag.h). */
#ifndef NBT_CAPTURE_SHIM_DIAG_H
#define NBT_CAPTURE_SHIM_DIAG_H

#include "nbt/capture/protocol.h"

#include <stdint.h>

/* Maps and clears the ring and records INIT; 0 or -1 (no ring, nothing recorded). */
int nbt_shim_diag_open(int observer);
int nbt_shim_diag_ready(void);
/* One record; the caller holds the shim's state lock. */
void nbt_shim_diag_record(uint8_t kind, uint8_t fblock, uint16_t fkt, uint8_t inst, uint8_t op,
                          uint64_t ts, uint64_t gen, uint8_t flags, uint8_t detail);
/* A BACK hold's length for a record's detail byte: 100 ms units, capped at 255. */
uint8_t nbt_shim_tenths(uint64_t ms);

#endif
