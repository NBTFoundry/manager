// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_FILES_LISTING_HPP
#define NBT_STORAGE_FILES_LISTING_HPP

#include "nbt/core/status.hpp"
#include "nbt/files_policy.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace storage {

struct FileEntry {
    char name[NBT_FILES_NAME_MAX + 1];
    uint64_t size;
};

struct FilesUsage {
    unsigned count;
    uint64_t bytes;
};

/* The stored files of `dir`: entries with a valid name that are regular,
 * owned by `owner` on `device`, with one link (anything else is not counted).
 * Fills up to `capacity` of those named after `after` ("" or NULL: from the
 * first), in byte order, and returns the count and size of all of them. */
core::Result<FilesUsage> listFolder(const char *dir, dev_t device, uid_t owner, const char *after,
                                    FileEntry *entries, size_t capacity, size_t *count);

} // namespace storage
} // namespace nbt

#endif
