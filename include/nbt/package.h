// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PACKAGE_H
#define NBT_PACKAGE_H

#include "nbt/bridge.h"
#include "nbt/settings.h"

#include <stdint.h>
#include <sys/types.h>

#define NBT_MANIFEST_MAX 2048u
#define NBT_PAYLOAD_MAX (64u * 1024u * 1024u)
#define NBT_SIGNATURE_SIZE 384u
#define NBT_ID_MAX 48u
#define NBT_VERSION_MAX 48u

typedef struct {
    char app[NBT_ID_MAX + 1];
    char package_id[33];
    char version[NBT_VERSION_MAX + 1];
    char compatibility[NBT_ID_MAX + 1];
    uint64_t sequence;
    uint64_t payload_size;
    unsigned char payload_hash[32];
    unsigned char package_hash[32];
    off_t payload_offset;
    /* Optional "setting=" lines after sha256 (see nbt/settings.h). */
    nbt_settings settings;
    nbt_bridge_requirement bridge;
} nbt_package;

/* Reads the application id a package declares, without checking its
 * signature: only to choose which provisioned application (profile and keys)
 * to verify it against with nbt_package_verify. The header, lengths and the
 * whole manifest must be well formed. Returns 0 or -1. */
int nbt_package_peek_app(int fd, char app[NBT_ID_MAX + 1]);
/* fd must be a seekable, regular file. The caller retains ownership of fd. */
int nbt_package_verify(int fd, int public_key_fd, const char *app_id, const char *compatibility,
                       nbt_package *out, char *error, unsigned error_size);
#endif
