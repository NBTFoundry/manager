// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_NET_POLICY_H
#define NBT_NET_POLICY_H

/* The car's packet-filter rules an application needs (capability `net`,
 * bridge 1.6), declared in its signed package as one `bridge-net=` line:
 *
 *   tcp:<port>@<interface>[+<interface>][,...]
 *
 * 1-4 rules for distinct ports 1-65535 (canonical decimal), each on one or
 * both of the car's Wi-Fi networks: `uap0` (the CarPlay hotspot) and `wfd0`
 * (the Wi-Fi Direct group), each at most once. The host lets those
 * connections in while the application runs (docs/app-bridge.md, "Network").
 * Allocation free. */

#include <stdint.h>

#define NBT_NET_POLICY_MAX 96 /* text bytes, without the terminator */
#define NBT_NET_RULES 4
#define NBT_NET_UAP0 1u
#define NBT_NET_WFD0 2u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t port;
    unsigned interfaces; /* NBT_NET_UAP0 | NBT_NET_WFD0 */
} nbt_net_rule;

typedef struct {
    unsigned count;
    nbt_net_rule rules[NBT_NET_RULES];
} nbt_net_policy;

/* 0 for a valid, canonical policy (`out` may be NULL to validate only). */
int nbt_net_policy_parse(const char *text, nbt_net_policy *out);
/* The interface's name for one NBT_NET_* bit, or NULL. */
const char *nbt_net_interface_name(unsigned bit);

#ifdef __cplusplus
}
#endif

#endif
