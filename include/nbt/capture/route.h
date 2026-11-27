// SPDX-License-Identifier: MPL-2.0
/* The audio connection iDrive is not to see (docs/input-capture.md, "The
 * media connection"): its holder's note, and the filter the capture shim runs
 * over ConnectionMaster's connection table on its way to iDrive. C99, no
 * heap. */
#ifndef NBT_CAPTURE_ROUTE_H
#define NBT_CAPTURE_ROUTE_H

#include "nbt/capture/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "NBTROUTE1 <pid> <connection, hex>\n": 0 and the two values, or -1. */
int nbt_capture_route_parse(const char *text, size_t size, int32_t *pid, uint16_t *conn);
int nbt_capture_route_format(int32_t pid, uint16_t conn, char *out, size_t size);
/* The note at `path`, with the checks of the shim's other /dev/shmem files. */
int nbt_capture_route_read(const char *path, int32_t *pid, uint16_t *conn);

/* What nbt_capture_table_hide found in a DSI data response. */
typedef enum {
    NBT_CAPTURE_TABLE_NONE = 0, /* another message, or no row of that connection */
    /* The whole table (position 0): its rows of `conn` were taken out in place
     * and the MOST data length shortened; to be forwarded. */
    NBT_CAPTURE_TABLE_REWRITTEN = 1,
    /* One row sent alone (position > 0), of `conn`: to be held back. */
    NBT_CAPTURE_TABLE_ROW = 2
} nbt_capture_table_result;

/* `bytes`/`length`: the stream from its read position, as
 * nbt_capture_classify_bytes takes it. Only ConnectionMaster's
 * HighSyncConnectionTableExt status (FBlock 0x03, 0xC03) in the layout
 * HmiMain3D reads is touched: two position bytes, then rows of a connection
 * id (big-endian) and one byte, at most 20. `rewrite` 0: reports without
 * changing a byte. */
nbt_capture_table_result nbt_capture_table_hide(unsigned char *bytes, size_t length, uint16_t conn,
                                                int rewrite);

#ifdef __cplusplus
}
#endif
#endif
