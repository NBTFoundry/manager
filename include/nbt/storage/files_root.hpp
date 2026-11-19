// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_FILES_ROOT_HPP
#define NBT_STORAGE_FILES_ROOT_HPP

/* Applications' files (capability `files`, docs/files-bridge.md), on the head
 * unit's hard disk rather than under the managed root:
 *
 *   <root>/files/<app>/<name>      what the application reads (NBT_FILES_DIR)
 *   <root>/staging/<app>.<source>  a transfer in progress, renamed into place
 *
 * Every directory is the manager's own, private (0700), on the root's device;
 * every file regular, owned and with one link, as under the managed root.
 * Only the worker writes; the application only reads its folder. */

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/files_policy.h"
#include "nbt/fs.h"
#include "nbt/storage/files_listing.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace storage {

enum class FilesSource { Phone, Usb };

class FilesRoot {
public:
    /* Space left free on the disk whatever the quotas allow. */
    static const uint64_t kReserveBytes = UINT64_C(256) * 1024 * 1024;

    FilesRoot();
    /* The car's location, /mnt/share/nbt-manager (NBT_TEST_FILES_ROOT in test
     * builds). */
    static const char *location();
    /* Opens `path`; when `create`, creates it and its two folders when absent. */
    core::Status open(const char *path, bool create = true);
    bool valid() const {
        return valid_;
    }
    const char *root() const {
        return fs_.root;
    }
    /* <root>/files/<app>, created when `create` and absent. */
    core::Status appDir(const char *app, bool create, core::Span<char> out) const;
    /* The application's usage, and up to `capacity` entries named after
     * `after` ("" or NULL: from the first) in byte order, in `entries`. */
    core::Result<FilesUsage> list(const char *app, const char *after, FileEntry *entries,
                                  size_t capacity, size_t *count) const;
    core::Result<uint64_t> freeBytes() const;
    /* Whether `name`, `size` bytes, may be stored now: its type and size by
     * the policy, the quota and file count counting a file it replaces, and
     * the disk's free space. The status's message says why not. */
    core::Status admit(const char *app, const nbt_files_policy &policy, const char *name,
                       uint64_t size) const;
    /* A fresh staging file for writing, replacing an abandoned one. */
    core::Result<core::UniqueFd> stage(const char *app, FilesSource source) const;
    /* Renames the (synced) staging file to `name`, replacing a file of that
     * name; nothing else of that name is replaced. */
    core::Status commit(const char *app, FilesSource source, const char *name) const;
    void discard(const char *app, FilesSource source) const;
    core::Status remove(const char *app, const char *name) const;
    /* The application's folder and staging files (uninstall). */
    core::Status removeApp(const char *app) const;

private:
    core::Status folder(const char *name, bool create) const;
    core::Status filePath(const char *app, const char *name, core::Span<char> out) const;
    core::Status stagingPath(const char *app, FilesSource source, core::Span<char> out) const;

    nbt_fs fs_;
    bool valid_;
};

} // namespace storage
} // namespace nbt

#endif
