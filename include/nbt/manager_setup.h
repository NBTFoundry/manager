// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MANAGER_SETUP_H
#define NBT_MANAGER_SETUP_H

#include "nbt/fs.h"
#include <stdint.h>

/* Explicit first enrollment. The root must already be created and validated;
 * release and key are workstation-provided regular files. The expected digest
 * and compatibility are bound by the authenticated desktop installer. */
int nbt_manager_setup(nbt_fs *fs, int release_fd, int key_fd, const char *compat,
                      const unsigned char expected_hash[32], char *error, unsigned error_size);

#endif
