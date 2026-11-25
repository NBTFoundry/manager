// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DIAG_POLICY_H
#define NBT_DIAG_POLICY_H

/* The control units an application may send diagnostic requests to
 * (capability `diag`, bridge 1.8), declared in its signed package as one
 * `bridge-diag=` line:
 *
 *   <ecu>:<class>[+<class>...][,<ecu>:...]
 *
 * 1-8 rules, ECU addresses as two lowercase hex digits in ascending order,
 * each with its classes in this order:
 *   read   0x22 read data, 0x2C 01/02/03 dynamic identifiers, 0x3E 00 tester present
 *   codes  0x19 read fault codes
 *   clear  0x14 clear fault codes, 0x10 01/03 session (for a clear), parked only
 * Every other request is refused (docs/diag-bridge.md). Allocation free. */

#include <stddef.h>
#include <stdint.h>

#define NBT_DIAG_POLICY_MAX 160 /* text bytes, without the terminator */
#define NBT_DIAG_RULES 8

#define NBT_DIAG_READ 1u
#define NBT_DIAG_CODES 2u
#define NBT_DIAG_CLEAR 4u

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
/* NBT_DIAG_* when the policy allows this request to this ECU, else 0. */
unsigned nbt_diag_policy_allows(const nbt_diag_policy *policy, unsigned ecu, const uint8_t *uds,
                                size_t size);

#ifdef __cplusplus
}
#endif

#endif
