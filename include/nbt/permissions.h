// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PERMISSIONS_H
#define NBT_PERMISSIONS_H

/* What a package may do, in the words the driver approves before it installs
 * (docs/permissions.md): derived from its signed bridge declaration, never
 * declared apart from it. Static: approved at install and on each update. */

#include "nbt/bridge.h"

#include <stddef.h>

#define NBT_PERM_BACKGROUND 0x001u /* `service` */
#define NBT_PERM_CAR_DATA 0x002u   /* `car-data` */
#define NBT_PERM_ENGINE 0x004u     /* `diag` read or codes */
#define NBT_PERM_FILES 0x008u      /* `files` */
#define NBT_PERM_PHONE_NET 0x010u  /* `net` */
#define NBT_PERM_MOST_READ 0x020u  /* `most` rx */
/* The dangerous ones, listed apart. */
#define NBT_PERM_MOST_SEND 0x040u /* `most` tx */
#define NBT_PERM_MOST_ANY 0x080u  /* `tx:*.*.*`, or a reserved function */
#define NBT_PERM_CLEAR 0x100u     /* `diag` clear */
#define NBT_PERM_CONTROL 0x200u   /* `diag` control */
#define NBT_PERM_DRIVE 0x400u     /* `diag` drive: clear or control while moving */
#define NBT_PERM_DANGEROUS 0x7C0u
#define NBT_PERM_ALL 0x7FFu

#ifdef __cplusplus
extern "C" {
#endif

/* NBT_PERM_* of a valid declaration (NULL: none). */
unsigned nbt_permissions(const nbt_bridge_requirement *bridge);
/* STATUS's field "perm:<hex>:<hex>": a package's permissions and those its
 * update adds (PROTOCOL.md). Format returns the length or -1; parse 0 or -1. */
int nbt_permissions_format(unsigned granted, unsigned added, char *out, size_t size);
int nbt_permissions_parse(const char *text, unsigned *granted, unsigned *added);

#ifdef __cplusplus
}
#endif

#endif
