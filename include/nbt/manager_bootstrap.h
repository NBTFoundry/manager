// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MANAGER_BOOTSTRAP_H
#define NBT_MANAGER_BOOTSTRAP_H
#include <stdint.h>
/* Reads exactly key_size bytes into `key`, then release_size bytes into
 * `release`, then EOF from `input`, within 15 s idle and 10 min total. */
int nbt_manager_receive_pair(int input, int key, uint64_t key_size, int release,
                             uint64_t release_size);
int nbt_manager_bootstrap(const char *root, int input, uint64_t key_size, uint64_t release_size,
                          const char *compat, const unsigned char digest[32], char *error,
                          unsigned error_size);
/* 1 on the supported head unit (always in host tests). */
int nbt_manager_platform_supported(void);
#endif
