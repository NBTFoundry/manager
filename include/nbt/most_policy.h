// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MOST_POLICY_H
#define NBT_MOST_POLICY_H

/* The MOST functions an application may use through the bridge (capability
 * `most`), declared in its signed package as one `bridge-most=` line and
 * enforced by its host. Canonical text, comma separated:
 *
 *   rx:<fblock>.<inst>.<fkt>   receive messages of these functions
 *   tx:<fblock>.<inst>.<fkt>   send commands (op 0-8) to these functions
 *   ctl:<fblock>.<inst>        register as device controller for this key
 *   sub:<fblock>.<inst>.<fkt>  host-managed notification, covered by rx
 *
 * <fblock> and <inst> are two upper-case hex digits, <fkt> three. `*` stands
 * for any instance or function; in rx rules also for any FBlock (a bus
 * monitor). Rules are unique; see docs/most-bridge.md. Allocation free. */

#include <stddef.h>
#include <stdint.h>

#define NBT_MOST_POLICY_MAX 640 /* text bytes, without the terminator */
#define NBT_MOST_POLICY_RULES 48
#define NBT_MOST_POLICY_CONTROLS 16
#define NBT_MOST_POLICY_SUBSCRIPTIONS 32
#define NBT_MOST_ANY (-1)

typedef enum {
    NBT_MOST_RX = 1,
    NBT_MOST_TX = 2,
    NBT_MOST_CTL = 3,
    NBT_MOST_SUB = 4
} nbt_most_rule_kind;

typedef struct {
    nbt_most_rule_kind kind;
    int fblock, inst, fkt; /* 0..0xFF, 0..0xFF, 0..0xFFF or NBT_MOST_ANY */
} nbt_most_rule;

typedef struct {
    unsigned count;
    nbt_most_rule rules[NBT_MOST_POLICY_RULES];
} nbt_most_policy;

/* Returns 0 for a valid, canonical, non-empty policy and fills `out` (which
 * may be NULL to validate only); -1 otherwise. */
int nbt_most_policy_parse(const char *text, nbt_most_policy *out);
/* Whether a message of this function may be delivered, or a command sent. */
int nbt_most_policy_receives(const nbt_most_policy *policy, unsigned fblock, unsigned inst,
                             unsigned fkt);
int nbt_most_policy_sends(const nbt_most_policy *policy, unsigned fblock, unsigned inst,
                          unsigned fkt);
/* The device controller keys (fblock << 8 | inst), in declaration order.
 * Returns their number (at most NBT_MOST_POLICY_CONTROLS). */
size_t nbt_most_policy_controls(const nbt_most_policy *policy,
                                uint16_t keys[NBT_MOST_POLICY_CONTROLS]);

#endif
