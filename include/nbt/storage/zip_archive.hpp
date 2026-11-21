// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_ZIP_ARCHIVE_HPP
#define NBT_STORAGE_ZIP_ARCHIVE_HPP

/* A zip archive's files, read from its central directory and written out one
 * by one (a game's archive imported from a USB stick, docs/files-bridge.md).
 * Stored and deflated entries (zlib, the head unit's libz.so.2); no zip64, no
 * encryption, no multi-disk archives: such an entry, or such an archive, is
 * left out. Each file's size and CRC-32 are checked as it is written. */

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace storage {

struct ZipEntry {
    char name[256]; // as in the archive, folders included ("dir/game.cue")
    uint64_t size;  // uncompressed
    uint64_t packed;
    uint32_t crc;
    uint64_t offset; // of its local header
    unsigned method; // 0 stored, 8 deflated
};

class ZipArchive {
public:
    static const unsigned kEntriesMax = 1024;

    ZipArchive();
    ~ZipArchive();
    ZipArchive(const ZipArchive &) = delete;
    ZipArchive &operator=(const ZipArchive &) = delete;

    /* False for a file that is not a readable zip archive (or a zip64 one). */
    bool open(const char *path);
    void close();
    unsigned count() const {
        return count_;
    }
    const ZipEntry &entry(unsigned i) const {
        return entries_[i];
    }
    /* Writes entry `i` to `out`; `progress(context, bytes)` after each chunk
     * written. 0, or -1 (unreadable, corrupt, a write that failed). */
    int extract(unsigned i, int out, void (*progress)(void *, uint64_t), void *context);
    /* The last part of `name` (after its last '/' or '\'). */
    static const char *baseName(const char *name);

private:
    bool readAt(uint64_t offset, unsigned char *data, size_t size);
    bool central(uint64_t offset, uint64_t size, unsigned total);

    int fd_;
    uint64_t fileSize_;
    unsigned count_;
    ZipEntry *entries_;
};

} // namespace storage
} // namespace nbt

#endif
