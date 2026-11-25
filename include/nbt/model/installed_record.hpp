// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MODEL_INSTALLED_RECORD_HPP
#define NBT_MODEL_INSTALLED_RECORD_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/storage/managed_root.hpp"
#include "nbt/transaction.h"

namespace nbt {
namespace model {

class InstalledRecord {
public:
    InstalledRecord() : value_() {
    }
    static core::Result<InstalledRecord> parse(core::Span<const char> text);
    static core::Result<InstalledRecord> load(const storage::ManagedRoot &root, const char *app);
    core::Result<size_t> format(core::Span<char> output) const;
    const nbt_installed &legacy() const {
        return value_;
    }

    static int parseFields(char **cursor, const char *prefix, nbt_installed *record, bool bridge);
    static unsigned recordLevel(const nbt_installed *record);
    static int levelOf(const char *text, const char *magic);
    static int readText(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, char *text,
                        size_t size);
    static int writeAll(int fd, const void *data, size_t size);
    static int atomicText(nbt_fs *fs, const char *app, nbt_fs_path_kind next,
                          nbt_fs_path_kind target, const char *text);
    static int writeRecord(nbt_fs *fs, const char *app, const nbt_installed *record);
    static int profileCompat(nbt_fs *fs, const char *app, char out[NBT_ID_MAX + 1]);
    /* trust/apps/<app>.conf: "NBTAPP1", "compat=<id>", then "signer=<key id>"
     * for an application a developer key signs (nbt/signers.h); `signer` is
     * "" for the apps key. */
    static int profileRead(nbt_fs *fs, const char *app, char compat[NBT_ID_MAX + 1],
                           char signer[65]);
    static int profileFormat(const char *compat, const char *signer, char *out, size_t size);
    static int copyCandidate(nbt_fs *fs, const char *app, const char *tx, int upload,
                             const nbt_package *package);

private:
    nbt_installed value_;
};

} // namespace model
} // namespace nbt

#endif
