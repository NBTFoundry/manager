// SPDX-License-Identifier: MPL-2.0
/* Bounded control and liveness record helpers for input capture. */
#ifndef NBT_CAPTURE_CONTROL_H
#define NBT_CAPTURE_CONTROL_H

#include "nbt/capture/protocol.h"

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void nbt_capture_control_init(nbt_capture_control *ctrl, int32_t gui_pid, uint64_t session_id,
                              uint64_t generation, const nbt_ccs_config *ccs);
int nbt_capture_control_valid(const nbt_capture_control *ctrl);
int nbt_capture_control_format(const nbt_capture_control *ctrl, char *out, size_t size);
int nbt_capture_control_parse(const char *text, size_t size, nbt_capture_control *out);

int nbt_capture_open_shmem_checked(const char *path, int writing);
int nbt_capture_control_write(const char *path, const nbt_capture_control *ctrl);
int nbt_capture_control_read(const char *path, nbt_capture_control *out);
int nbt_capture_liveness_read(const char *path, pid_t expected_pid, uint64_t *out_ts_ms);

#ifdef __cplusplus
}
#endif
#endif
