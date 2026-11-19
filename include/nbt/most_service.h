#ifndef NBT_MOST_SERVICE_H
#define NBT_MOST_SERVICE_H

/* Bridge capability `most`, host side: one MOST client per application host,
 * limited to the application's signed policy (include/nbt/most_policy.h) and
 * relayed over its datagram socket (include/nbt/most_wire.h). Messages are
 * forwarded from the DSI connection thread; requests are handled when the
 * host's main loop calls nbt_most_service_poll. See docs/most-bridge.md. */

#include "nbt/most_policy.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_MOST_SEND_RATE 100u /* commands per second, with a burst of as many */

/* Creates the application's socket pair. `host` is non-blocking and
 * close-on-exec; `app` is close-on-exec (the spawner duplicates it to the
 * child). Both have buffers for full records. Returns 0 or -1. */
int nbt_most_service_socket(int *host, int *app);
/* Starts the client for `app` (registered as NBTAPP_<app>) with the parsed
 * policy and the host's socket ends, which it keeps: `host` for capability
 * `most` and `controller` for capability `controller` (include/nbt/
 * controller_wire.h, decoded before the policy applies; its device
 * controllers are registered too). Either may be -1, not both. Once per
 * process. `library` NULL selects the head unit's DSI library. Returns 0, or
 * -1 when the client cannot start (both capabilities are then unavailable). */
int nbt_most_service_start(const char *library, const char *app, const nbt_most_policy *policy,
                           int host, int controller);
/* Handles every pending request on the socket. Returns 0, or -1 when the
 * socket failed for good. `now_ms` is monotonic, for the send rate limit. */
int nbt_most_service_poll(uint64_t now_ms);
/* The host's socket end, for poll(); -1 before start. */
int nbt_most_service_fd(void);
/* Best-effort per-client ClearFunction on clean exit, at most 400 ms of
 * rate-limit waiting. Process death relies on server disconnect cleanup. */
void nbt_most_service_stop(void);
/* Nonzero: discard messages and controller states for the application
 * instead of sending them; resuming sends the current controller state. */
void nbt_most_service_pause(int paused);
/* Nonzero: the application has a surface, so controller states may reach it
 * (off at start: a background application never reads the controller). */
void nbt_most_service_controller_shown(int shown);

#ifdef __cplusplus
}
#endif

#endif
