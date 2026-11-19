// SPDX-License-Identifier: MPL-2.0
#include "nbt/model/installed_record.hpp"

#include "nbt/core/text.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace model {
namespace {

bool validVersion(const char *s) {
    return nbt_token_valid(s, NBT_VERSION_MAX, NBT_TOKEN_UPPER | NBT_TOKEN_DOT) != 0;
}

bool validHash(const char *s) {
    unsigned char value[32];
    return s && !nbt_hex_decode(s, value, sizeof(value));
}

} // namespace

int InstalledRecord::writeAll(int fd, const void *data, size_t size) {
    const unsigned char *p = static_cast<const unsigned char *>(data);
    while (size) {
        const ssize_t w = write(fd, p, size);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            return -1;
        }
        p += w;
        size -= static_cast<size_t>(w);
    }
    return 0;
}

int InstalledRecord::readText(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, char *text,
                              size_t size) {
    size_t used = 0;
    if (size < 2) {
        errno = EINVAL;
        return -1;
    }
    const int fd = nbt_fs_open(fs, kind, app, NULL, NBT_FS_READ);
    if (fd < 0) {
        return -1;
    }
    while (used < size - 1) {
        const ssize_t n = read(fd, text + used, size - 1 - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (!n) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    char extra = 0;
    ssize_t n = 0;
    do {
        n = read(fd, &extra, 1);
    } while (n < 0 && errno == EINTR);
    close(fd);
    if (n != 0 || memchr(text, 0, used)) {
        errno = EINVAL;
        return -1;
    }
    text[used] = 0;
    return 0;
}

int InstalledRecord::atomicText(nbt_fs *fs, const char *app, nbt_fs_path_kind next,
                                nbt_fs_path_kind target, const char *text) {
    const int fd = nbt_fs_open(fs, next, app, NULL, NBT_FS_CREATE);
    if (fd < 0) {
        return -1;
    }
    if (writeAll(fd, text, strlen(text)) || fsync(fd) ||
        nbt_fs_rename(fs, next, target, app, NULL) || fsync(fd) || nbt_fs_barrier(fs, app, NULL)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return close(fd);
}

int InstalledRecord::parseFields(char **cursor, const char *prefix, nbt_installed *record,
                                 bool bridge) {
    char name[32];
    uint64_t value = 0;
    memset(record, 0, sizeof(*record));
    if (snprintf(name, sizeof(name), "%ssequence", prefix) >= static_cast<int>(sizeof(name))) {
        return -1;
    }
    char *s = nbt_field(cursor, name);
    if (!s || strlen(s) != 20 || nbt_parse_u64(s, &value, 1) || !value) {
        return -1;
    }
    record->sequence = static_cast<unsigned long long>(value);
    snprintf(name, sizeof(name), "%shighwater", prefix);
    s = nbt_field(cursor, name);
    if (!s || strlen(s) != 20 || nbt_parse_u64(s, &value, 1) || value < record->sequence) {
        return -1;
    }
    record->highwater = static_cast<unsigned long long>(value);
    snprintf(name, sizeof(name), "%sversion", prefix);
    s = nbt_field(cursor, name);
    if (!validVersion(s)) {
        return -1;
    }
    memcpy(record->version, s, strlen(s) + 1);
    snprintf(name, sizeof(name), "%ssha256", prefix);
    s = nbt_field(cursor, name);
    if (!validHash(s)) {
        return -1;
    }
    memcpy(record->sha256, s, 65);
    return bridge ? nbt_bridge_requirement_parse(cursor, prefix, &record->bridge) : 0;
}

// 1 no bridge, 2 bridge, 3 with a MOST policy, 4 with a files policy (a
// release before bridge 1.5 refuses level 4 rather than drop the policy).
unsigned InstalledRecord::recordLevel(const nbt_installed *record) {
    return record->bridge.files[0]  ? 4
           : record->bridge.most[0] ? 3
                                    : (record->bridge.major ? 2 : 1);
}

int InstalledRecord::levelOf(const char *text, const char *magic) {
    const size_t n = strlen(magic);
    if (strncmp(text, magic, n) || text[n] < '1' || text[n] > '4' || text[n + 1] != '\n') {
        return -1;
    }
    return text[n] - '0';
}

int InstalledRecord::writeRecord(nbt_fs *fs, const char *app, const nbt_installed *record) {
    char text[512 + NBT_BRIDGE_TEXT_MAX];
    if (nbt_tx_record_format(record, text, sizeof(text)) < 0) {
        errno = EINVAL;
        return -1;
    }
    return atomicText(fs, app, NBT_FS_RECORD_NEXT, NBT_FS_RECORD, text);
}

int InstalledRecord::profileCompat(nbt_fs *fs, const char *app, char out[NBT_ID_MAX + 1]) {
    char text[128];
    if (readText(fs, NBT_FS_PROFILE, app, text, sizeof(text)) || strncmp(text, "NBTAPP1\n", 8)) {
        return -1;
    }
    char *p = text + 8;
    char *value = nbt_field(&p, "compat");
    if (!nbt_token_valid(value, NBT_ID_MAX, NBT_TOKEN_DOT) || *p) {
        return -1;
    }
    memcpy(out, value, strlen(value) + 1);
    return 0;
}

int InstalledRecord::copyCandidate(nbt_fs *fs, const char *app, const char *tx, int upload,
                                   const nbt_package *package) {
    unsigned char buf[8192];
    unsigned char digest[32];
    uint64_t remaining = package->payload_size;
    SHA256_CTX sha;
    const int fd = nbt_fs_open(fs, NBT_FS_CANDIDATE, app, tx, NBT_FS_CREATE);
    if (fd < 0) {
        return -1;
    }
    if (lseek(upload, package->payload_offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }
    SHA256_Init(&sha);
    while (remaining) {
        const size_t want = remaining < sizeof(buf) ? static_cast<size_t>(remaining) : sizeof(buf);
        const ssize_t n = read(upload, buf, want);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0 || writeAll(fd, buf, static_cast<size_t>(n))) {
            close(fd);
            return -1;
        }
        SHA256_Update(&sha, buf, static_cast<size_t>(n));
        remaining -= static_cast<size_t>(n);
    }
    SHA256_Final(digest, &sha);
    if (memcmp(digest, package->payload_hash, sizeof(digest)) || fchmod(fd, 0700) || fsync(fd)) {
        close(fd);
        errno = EIO;
        return -1;
    }
    return close(fd);
}

core::Result<InstalledRecord> InstalledRecord::parse(core::Span<const char> text) {
    char copy[512 + NBT_BRIDGE_TEXT_MAX];
    InstalledRecord result;
    if (!text.data() || !core::copyTerminated(core::StringRef(text.data(), text.size()),
                                              core::Span<char>(copy, sizeof(copy)))
                             .ok()) {
        return core::Result<InstalledRecord>::failure(
            core::Status::error(EINVAL, "invalid installed record size"));
    }
    if (nbt_tx_record_parse(copy, &result.value_)) {
        return core::Result<InstalledRecord>::failure(
            core::Status::error(EINVAL, "invalid installed record"));
    }
    return core::Result<InstalledRecord>::success(result);
}

core::Result<InstalledRecord> InstalledRecord::load(const storage::ManagedRoot &root,
                                                    const char *app) {
    InstalledRecord result;
    if (!root.valid() ||
        nbt_tx_installed(const_cast<nbt_fs *>(root.legacy()), app, &result.value_)) {
        return core::Result<InstalledRecord>::failure(
            core::Status::fromErrno("installed record load failed"));
    }
    return core::Result<InstalledRecord>::success(result);
}

core::Result<size_t> InstalledRecord::format(core::Span<char> output) const {
    const int result = nbt_tx_record_format(&value_, output.data(), output.size());
    if (result < 0) {
        return core::Result<size_t>::failure(
            core::Status::error(EINVAL, "invalid installed record or output size"));
    }
    return core::Result<size_t>::success(static_cast<size_t>(result));
}

} // namespace model
} // namespace nbt

int nbt_tx_record_parse(char *text, nbt_installed *record) {
    if (!text || !record) {
        return -1;
    }
    const int level = nbt::model::InstalledRecord::levelOf(text, "NBTSTATE");
    if (level < 0) {
        return -1;
    }
    char *cursor = text + 10;
    if (nbt::model::InstalledRecord::parseFields(&cursor, "", record, level > 1) || *cursor ||
        nbt::model::InstalledRecord::recordLevel(record) != static_cast<unsigned>(level)) {
        return -1;
    }
    return 0;
}

int nbt_tx_record_format(const nbt_installed *record, char *text, size_t size) {
    char bridge[NBT_BRIDGE_TEXT_MAX];
    if (!record || !text ||
        nbt_bridge_requirement_format(&record->bridge, "", bridge, sizeof(bridge)) < 0) {
        return -1;
    }
    const int n = snprintf(
        text, size, "NBTSTATE%u\nsequence=%020llu\nhighwater=%020llu\nversion=%s\nsha256=%s\n%s",
        nbt::model::InstalledRecord::recordLevel(record), record->sequence, record->highwater,
        record->version, record->sha256, bridge);
    return (n < 0 || static_cast<size_t>(n) >= size) ? -1 : n;
}

int nbt_tx_installed(nbt_fs *fs, const char *app, nbt_installed *record) {
    char text[512 + NBT_BRIDGE_TEXT_MAX];
    if (nbt::model::InstalledRecord::readText(fs, NBT_FS_RECORD, app, text, sizeof(text)) ||
        nbt_tx_record_parse(text, record)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
