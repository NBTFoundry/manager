// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/files_listing.hpp"

#include "nbt/fs.h"
#include "nbt/storage/path_open.hpp"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace nbt {
namespace storage {
namespace {

/* Keeps the `capacity` first names in byte order (insertion: at most a few
 * hundred files, a page of a few dozen). */
void keep(const char *name, uint64_t size, FileEntry *entries, size_t capacity, size_t *count) {
    size_t i = *count;
    if (i == capacity) {
        if (strcmp(name, entries[capacity - 1].name) >= 0) {
            return;
        }
        --i;
    } else {
        ++*count;
    }
    for (; i > 0 && strcmp(entries[i - 1].name, name) > 0; --i) {
        entries[i] = entries[i - 1];
    }
    memcpy(entries[i].name, name, strlen(name) + 1);
    entries[i].size = size;
}

} // namespace

core::Result<FilesUsage> listFolder(const char *dir, dev_t device, uid_t owner, const char *after,
                                    FileEntry *entries, size_t capacity, size_t *count) {
    FilesUsage usage = {0, 0};
    char path[NBT_PATH_MAX];
    *count = 0;
    DIR *directory = opendir(dir);
    if (!directory) {
        return core::Result<FilesUsage>::failure(core::Status::fromErrno("cannot list files"));
    }
    errno = 0;
    for (struct dirent *entry; (entry = readdir(directory)) != NULL; errno = 0) {
        const char *name = entry->d_name;
        struct stat st;
        const int n = snprintf(path, sizeof(path), "%s/%s", dir, name);
        if (!nbt_files_name_valid(name) || n < 0 || static_cast<size_t>(n) >= sizeof(path) ||
            SafePathOpen::checkOwnedFile(device, owner, path) || lstat(path, &st)) {
            continue; // anything else is not a stored file
        }
        ++usage.count;
        usage.bytes += static_cast<uint64_t>(st.st_size);
        if (capacity && entries && !(after && after[0] && strcmp(name, after) <= 0)) {
            keep(name, static_cast<uint64_t>(st.st_size), entries, capacity, count);
        }
    }
    const int saved = errno;
    closedir(directory);
    if (saved) {
        errno = saved;
        return core::Result<FilesUsage>::failure(core::Status::fromErrno("cannot list files"));
    }
    return core::Result<FilesUsage>::success(usage);
}

} // namespace storage
} // namespace nbt
