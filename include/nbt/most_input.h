// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MOST_INPUT_H
#define NBT_MOST_INPUT_H

/* iDrive controller input over MOST. Loads the head unit's DSI library at run
 * time, registers with MOSTSubSystem.DSYSMOSTServer as a MOST client and device
 * controller, and decodes controller status messages on the library's
 * connection thread. The ABI used here is reconstructed and unverified on the
 * target; see docs/controller-input.md. */

#include "nbt/ccs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the client once per process. `library` NULL selects the target paths.
 * Returns a non-blocking descriptor that yields one byte (an nbt_input value)
 * per input, or -1. Input is dropped if the reader falls behind. */
int nbt_most_input_start(const char *library, const nbt_ccs_config *config);
/* Hands the controllers back before the process exits, when the config's
 * `release` is set: the library's thread sends request 0x5 for the registered
 * instances at its next callback and stops delivering input. Waits at most
 * `timeout_ms`. Returns 1 once done (sent, or nothing was registered), 0 when
 * release is off or the client never started, and -1 on timeout. */
int nbt_most_input_stop(unsigned timeout_ms);
/* Non-zero while the controller BACK key (or left tilt) is currently held. */
int nbt_most_input_back_held(void);
/* Non-zero while a hard BACK button slot is currently held (excludes left tilt). */
int nbt_most_input_back_key_held(void);
/* Writes the last controller messages received (raw, oldest first) to the
 * debug log and forgets them (nbt/most/input_trace.hpp). */
void nbt_most_input_log_recent(const char *why);
/* Non-blocking descriptor woken with one byte when AudioMaster 0x20/0x0C08
 * reports a volume change, or -1 if unavailable. */
int nbt_most_input_volume_fd(void);
/* Latest volume percentage (0..100) and monotonic change sequence; returns 1
 * once a volume message has arrived, 0 before any is seen. */
int nbt_most_input_volume(int *percent, uint32_t *sequence);

#ifdef __cplusplus
}
#endif
#endif
