// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CDS_POLICY_H
#define NBT_CDS_POLICY_H

/* The iDrive car data an application reads (capability `car-data`, bridge
 * 1.6), declared in its signed package as one `bridge-cds=` line:
 *
 *   <name>[,<name>...]
 *
 * 1-16 unique car-data property names (iDrive's dotted names, such as
 * `driving.gear`) of 3-64 bytes `[A-Za-z0-9_.]`, each with a '.' that is
 * neither first nor last, and no "..". The plugin subscribes to them while
 * the application runs and publishes each value as the car gives it
 * (docs/app-bridge.md, "Car data"). Allocation free. */

#include <stddef.h>

#define NBT_CDS_POLICY_MAX 255 /* text bytes, without the terminator */
#define NBT_CDS_NAMES 16
#define NBT_CDS_NAME_MAX 64

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned count;
    char names[NBT_CDS_NAMES][NBT_CDS_NAME_MAX + 1];
} nbt_cds_policy;

/* 0 for a valid, canonical policy (`out` may be NULL to validate only). */
int nbt_cds_policy_parse(const char *text, nbt_cds_policy *out);
/* Whether `name` (`length` bytes, no terminator needed) is a valid name. */
int nbt_cds_name_valid(const char *name, size_t length);

#ifdef __cplusplus
}
#endif

#endif
