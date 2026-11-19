// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_REMOVE_TREE_HPP
#define NBT_STORAGE_REMOVE_TREE_HPP

#include <stddef.h>
#include <sys/types.h>

namespace nbt {
namespace storage {

/* Removes `path` (`length` bytes in a NBT_PATH_MAX buffer, restored on
 * return) and everything under it. A link is unlinked, never entered; a
 * directory on another device than `device`, or deeper than 16 levels, is
 * refused. The owner is stopped and its directories are private (0700), so
 * nothing else is expected to change the tree meanwhile. Entries are removed
 * in passes until a pass finds none, as removing while reading a directory
 * may skip entries. 0, or -1 with errno. */
int removeTree(char *path, size_t length, dev_t device, unsigned depth);

} // namespace storage
} // namespace nbt

#endif
