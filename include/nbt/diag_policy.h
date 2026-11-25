// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DIAG_POLICY_H
#define NBT_DIAG_POLICY_H

/* The control units an application may send diagnostic requests to
 * (capability `diag`, bridge 1.8; `control` and `drive` 1.9), declared in its
 * signed package as one `bridge-diag=` line and shown to the driver as
 * permissions before it installs (docs/permissions.md):
 *
 *   <ecu>:<class>[+<class>...][,<ecu>:...]
 *
 * 1-8 rules, ECU addresses as two lowercase hex digits in ascending order,
 * each with its classes in this order:
 *   read     0x22 read data, 0x2C 01/02/03 dynamic identifiers, 0x3E 00 tester present
 *   codes    0x19 read fault codes
 *   clear    0x14 clear fault codes, parked only
 *   control  0x2E write data, 0x2F input/output control, 0x31 01/02/03 routines,
 *            parked only
 *   drive    clear and control also while the car moves (with one of them)
 * clear and control allow 0x10 01/03 (default and extended session). Every
 * other request is refused: security access, programming, resets
 * (docs/diag-bridge.md). Allocation free. */

#include <stddef.h>
#include <stdint.h>

#define NBT_DIAG_POLICY_MAX 160 /* text bytes, without the terminator */
#define NBT_DIAG_RULES 8

#define NBT_DIAG_READ 1u
#define NBT_DIAG_CODES 2u
#define NBT_DIAG_CLEAR 4u
#define NBT_DIAG_CONTROL 8u
#define NBT_DIAG_DRIVE 16u   /* a rule's modifier, never a request's class */
#define NBT_DIAG_SESSION 32u /* a request's class only: allowed with clear or control */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned count;
    struct {
        uint8_t ecu;
        uint8_t classes; /* NBT_DIAG_* */
    } rules[NBT_DIAG_RULES];
} nbt_diag_policy;

/* 0 for a valid, canonical policy (`out` may be NULL to validate only). */
int nbt_diag_policy_parse(const char *text, nbt_diag_policy *out);
/* The class a UDS request belongs to (NBT_DIAG_*), or 0 for one never allowed. */
unsigned nbt_diag_request_class(const uint8_t *uds, size_t size);
/* NBT_DIAG_* when the policy allows this request to this ECU, else 0: the
 * request's class, with NBT_DIAG_DRIVE when the rule allows it while moving. */
unsigned nbt_diag_policy_allows(const nbt_diag_policy *policy, unsigned ecu, const uint8_t *uds,
                                size_t size);

#ifdef __cplusplus
}
#endif

#endif
