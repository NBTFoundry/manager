// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DSI_H
#define NBT_DSI_H

/* One MOSTSubSystem.DSYSMOSTServer 4.5 client per process, through the head
 * unit's DSI library loaded at run time (reconstructed ABI; see
 * docs/controller-input.md and docs/head-unit-interfaces.md). Callbacks run on
 * the library's connection thread, one after another. Requests and
 * notifications may be sent from any thread: one lock serialises them, as
 * NBT-AAD did on cars when sending from its own thread. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*attached)(void *user);
    void (*detached)(void *user);
    /* `payload` is valid only during the call; length at most NBT_DSI_PAYLOAD_MAX. */
    void (*data)(uint32_t id, const unsigned char *payload, uint32_t length, void *user);
    void *user;
} nbt_dsi_handlers;

#define NBT_DSI_PAYLOAD_MAX 8192u

/* Loads the library (`library` NULL selects the target paths), constructs the
 * client and starts its connection thread. Once per process: later calls fail
 * and leave the running client alone. Returns 0 or -1. */
int nbt_dsi_start(const char *library, const nbt_dsi_handlers *handlers);
/* Sends a request; -1 while not attached or if the library's send fails. */
int nbt_dsi_request(unsigned request, const unsigned char *payload, size_t length);
/* Subscribes to an attribute or information id; -1 while not attached. */
int nbt_dsi_notify(uint32_t attribute);

#ifdef __cplusplus
}
#endif

#endif
