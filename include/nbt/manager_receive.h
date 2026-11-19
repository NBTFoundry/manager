#ifndef NBT_MANAGER_RECEIVE_H
#define NBT_MANAGER_RECEIVE_H

#include "nbt/fs.h"
#include <stdint.h>

/* Read one bounded release from input_fd, verify it using the local manager
 * key, then stage the digest-bound update. The input must end exactly at size. */
int nbt_manager_receive_update(nbt_fs *fs, int input_fd, uint64_t size,
                               const unsigned char expected_hash[32], int recovery, char *error,
                               unsigned error_size);

#endif
