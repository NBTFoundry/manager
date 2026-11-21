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

bool gameFolder(const char *path, const char *name, dev_t device, uid_t owner) {
    struct stat st;
    return nbt_files_folder_valid(name) && !lstat(path, &st) && S_ISDIR(st.st_mode) &&
           st.st_dev == device && st.st_uid == owner && !(st.st_mode & 0077);
}

core::Result<FilesUsage> listLevel(const char *dir, dev_t device, uid_t owner, const char *after,
                                   FileEntry *entries, size_t capacity, size_t *count,
                                   bool folders) {
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
        if (n < 0 || static_cast<size_t>(n) >= sizeof(path)) {
            continue;
        }
        char shown[NBT_FILES_NAME_MAX + 1];
        uint64_t size = 0;
        if (folders && gameFolder(path, name, device, owner)) {
            // A game folder (one level): its files count, it is listed as "<name>/".
            size_t none = 0;
            core::Result<FilesUsage> inside =
                listLevel(path, device, owner, NULL, NULL, 0, &none, false);
            if (!inside.ok()) {
                continue;
            }
            usage.count += inside.value().count;
            size = inside.value().bytes;
            const size_t length = strlen(name); // valid: at most NBT_FILES_FOLDER_MAX
            memcpy(shown, name, length);
            shown[length] = '/';
            shown[length + 1] = 0;
        } else if (!nbt_files_name_valid(name) ||
                   SafePathOpen::checkOwnedFile(device, owner, path) || lstat(path, &st)) {
            continue; // anything else is not a stored file
        } else {
            ++usage.count;
            size = static_cast<uint64_t>(st.st_size);
            memcpy(shown, name, strlen(name) + 1); // valid: at most NBT_FILES_NAME_MAX
        }
        usage.bytes += size;
        if (capacity && entries && !(after && after[0] && strcmp(shown, after) <= 0)) {
            keep(shown, size, entries, capacity, count);
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

} // namespace

core::Result<FilesUsage> listFolder(const char *dir, dev_t device, uid_t owner, const char *after,
                                    FileEntry *entries, size_t capacity, size_t *count) {
    return listLevel(dir, device, owner, after, entries, capacity, count, true);
}

} // namespace storage
} // namespace nbt
