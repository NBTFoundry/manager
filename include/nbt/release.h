// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_RELEASE_H
#define NBT_RELEASE_H

#include "nbt/bridge.h"

#include <stdint.h>
#include <sys/types.h>

#define NBT_RELEASE_V1_COMPONENTS 8
#define NBT_RELEASE_MAX_COMPONENTS 9
#define NBT_RELEASE_CAPTURE_INDEX 8
#define NBT_RELEASE_MANIFEST_MAX 4096u
#define NBT_RELEASE_PAYLOAD_MAX (128u * 1024u * 1024u)

typedef struct {
    uint64_t size;
    off_t offset;
    unsigned char sha256[32];
} nbt_release_component;

typedef struct {
    char version[49];
    char compatibility[49];
    uint64_t sequence;
    uint8_t format_version;
    uint8_t component_count;
    nbt_bridge_provider bridge;
    unsigned char sha256[32];
    nbt_release_component components[NBT_RELEASE_MAX_COMPONENTS];
} nbt_release;

/* Fixed component order is part of the format, never a destination path. */
const char *nbt_release_component_name(unsigned index);
#ifdef __cplusplus
extern "C"
#endif
    int nbt_release_verify(int fd, int manager_key_fd, const char *compatibility, nbt_release *out,
                           char *error, unsigned error_size);

/* Copy one previously verified component to a new, empty regular file.
 * The caller obtains the output descriptor through path policy and removes
 * the untrusted partial file after any error. The caller owns both descriptors. */
int nbt_release_copy_component(int fd, const nbt_release *verified, unsigned index, int output_fd,
                               char *error, unsigned error_size);

#endif
