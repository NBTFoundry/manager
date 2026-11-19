#include "nbt/lifecycle/transaction_journal.hpp"

#include "nbt/core/fault.hpp"
#include "nbt/core/text.hpp"
#include "nbt/model/installed_record.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace lifecycle {
namespace {

using model::InstalledRecord;

bool validTx(const char *s) {
    unsigned char value[16];
    return s && !nbt_hex_decode(s, value, sizeof(value));
}

bool validPhase(const char *s) {
    return s && (!strcmp(s, "PREPARED") || !strcmp(s, "BACKED_UP") || !strcmp(s, "INSTALLED") ||
                 !strcmp(s, "CONFIRMED"));
}

} // namespace

TransactionJournal::TransactionJournal() : tx_(), phase_(), oldRecord_(), newRecord_() {
}

void TransactionJournal::reset(const nbt_installed &oldRecord, const nbt_package &package) {
    memset(tx_, 0, sizeof(tx_));
    memset(phase_, 0, sizeof(phase_));
    oldRecord_ = oldRecord;
    memset(&newRecord_, 0, sizeof(newRecord_));
    newRecord_.bridge = package.bridge;
    newRecord_.sequence = static_cast<unsigned long long>(package.sequence);
    newRecord_.highwater =
        oldRecord.highwater > package.sequence ? oldRecord.highwater : package.sequence;
    memcpy(newRecord_.version, package.version, strlen(package.version) + 1);
}

int TransactionJournal::parse(char *text) {
    const int level = InstalledRecord::levelOf(text, "NBTJ");
    if (level < 0) {
        return -1;
    }
    char *p = text + 6;
    char *value = nbt_field(&p, "tx");
    if (!validTx(value)) {
        return -1;
    }
    memcpy(tx_, value, sizeof(tx_));
    value = nbt_field(&p, "phase");
    if (!validPhase(value)) {
        return -1;
    }
    memcpy(phase_, value, strlen(value) + 1);
    const bool bridge = level > 1;
    if (InstalledRecord::parseFields(&p, "old_", &oldRecord_, bridge) ||
        InstalledRecord::parseFields(&p, "new_", &newRecord_, bridge) || *p ||
        newRecord_.highwater < oldRecord_.highwater) {
        return -1;
    }
    const unsigned oldLvl = InstalledRecord::recordLevel(&oldRecord_);
    const unsigned newLvl = InstalledRecord::recordLevel(&newRecord_);
    const unsigned used = oldLvl > newLvl ? oldLvl : newLvl;
    return used == static_cast<unsigned>(level) ? 0 : -1;
}

int TransactionJournal::load(nbt_fs *fs, const char *app) {
    char text[1024 + 2 * NBT_BRIDGE_TEXT_MAX];
    if (InstalledRecord::readText(fs, NBT_FS_JOURNAL, app, text, sizeof(text)) || parse(text)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int TransactionJournal::writePhase(nbt_fs *fs, const char *app, const char *phase) {
    char text[1024 + 2 * NBT_BRIDGE_TEXT_MAX];
    char oldBridge[NBT_BRIDGE_TEXT_MAX];
    char newBridge[NBT_BRIDGE_TEXT_MAX];
    snprintf(phase_, sizeof(phase_), "%s", phase);
    if (nbt_bridge_requirement_format(&oldRecord_.bridge, "old_", oldBridge, sizeof(oldBridge)) <
            0 ||
        nbt_bridge_requirement_format(&newRecord_.bridge, "new_", newBridge, sizeof(newBridge)) <
            0) {
        return -1;
    }
    const unsigned oldLvl = InstalledRecord::recordLevel(&oldRecord_);
    const unsigned newLvl = InstalledRecord::recordLevel(&newRecord_);
    const int n = snprintf(
        text, sizeof(text),
        "NBTJ%u\ntx=%s\nphase=%s\n"
        "old_sequence=%020llu\nold_highwater=%020llu\nold_version=%s\nold_sha256=%s\n%s"
        "new_sequence=%020llu\nnew_highwater=%020llu\nnew_version=%s\nnew_sha256=%s\n%s",
        oldLvl > newLvl ? oldLvl : newLvl, tx_, phase_, oldRecord_.sequence, oldRecord_.highwater,
        oldRecord_.version, oldRecord_.sha256, oldBridge, newRecord_.sequence, newRecord_.highwater,
        newRecord_.version, newRecord_.sha256, newBridge);
    if (n < 0 || n >= static_cast<int>(sizeof(text))) {
        errno = EINVAL;
        return -1;
    }
    return InstalledRecord::atomicText(fs, app, NBT_FS_JOURNAL_NEXT, NBT_FS_JOURNAL, text);
}

int TransactionJournal::fileHash(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx,
                                 char out[65]) {
    unsigned char bytes[8192];
    unsigned char digest[32];
    SHA256_CTX ctx;
    const int fd = nbt_fs_open(fs, kind, app, tx, NBT_FS_READ);
    if (fd < 0) {
        return -1;
    }
    SHA256_Init(&ctx);
    for (;;) {
        const ssize_t n = read(fd, bytes, sizeof(bytes));
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
        SHA256_Update(&ctx, bytes, static_cast<size_t>(n));
    }
    close(fd);
    SHA256_Final(digest, &ctx);
    return nbt_hex(digest, sizeof(digest), out, 65);
}

int TransactionJournal::errorOut(char *error, unsigned size, const char *message) {
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return -1;
}

int TransactionJournal::confirm(nbt_fs *fs, const char *app, char *error, unsigned errorSize) {
    TransactionJournal j;
    char active[65];
    if (j.load(fs, app) || strcmp(j.phase(), "INSTALLED") ||
        fileHash(fs, NBT_FS_ACTIVE, app, NULL, active) || strcmp(active, j.newRecord().sha256)) {
        return errorOut(error, errorSize, "cannot confirm transaction");
    }
    if (j.writePhase(fs, app, "CONFIRMED")) {
        return errorOut(error, errorSize, "cannot persist confirmation");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "confirmed");
    if (InstalledRecord::writeRecord(fs, app, &j.newRecord())) {
        return errorOut(error, errorSize, "cannot persist installed version");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "recorded");
    if (nbt_fs_unlink(fs, NBT_FS_JOURNAL, app, NULL) || nbt_fs_barrier(fs, app, j.tx())) {
        return errorOut(error, errorSize, "cannot complete transaction");
    }
    if (errorSize) {
        *error = 0;
    }
    return 0;
}

int TransactionJournal::recover(nbt_fs *fs, const char *app, char *error, unsigned errorSize) {
    TransactionJournal j;
    char hash[65];
    char text[1024 + 2 * NBT_BRIDGE_TEXT_MAX];
    int exists = nbt_fs_exists(fs, NBT_FS_JOURNAL, app, NULL);
    int next = nbt_fs_exists(fs, NBT_FS_JOURNAL_NEXT, app, NULL);
    if (exists < 0 || next < 0) {
        return errorOut(error, errorSize, "unsafe journal path");
    }
    if (!exists && next) {
        if (InstalledRecord::readText(fs, NBT_FS_JOURNAL_NEXT, app, text, sizeof(text)) ||
            j.parse(text) || strcmp(j.phase(), "PREPARED") ||
            nbt_fs_rename(fs, NBT_FS_JOURNAL_NEXT, NBT_FS_JOURNAL, app, NULL)) {
            return errorOut(error, errorSize, "incomplete or invalid journal");
        }
        exists = 1;
        next = 0;
    }
    if (!exists) {
        if (nbt_fs_exists(fs, NBT_FS_RECORD_NEXT, app, NULL) != 0) {
            return errorOut(error, errorSize, "unexpected pending record");
        }
        if (errorSize) {
            *error = 0;
        }
        return 0;
    }
    if (j.load(fs, app)) {
        return errorOut(error, errorSize, "invalid transaction journal");
    }
    if (next && nbt_fs_unlink(fs, NBT_FS_JOURNAL_NEXT, app, NULL)) {
        return errorOut(error, errorSize, "cannot remove pending journal");
    }
    const int recordNext = nbt_fs_exists(fs, NBT_FS_RECORD_NEXT, app, NULL);
    if (recordNext < 0 || (recordNext && nbt_fs_unlink(fs, NBT_FS_RECORD_NEXT, app, NULL))) {
        return errorOut(error, errorSize, "unsafe pending record");
    }
    const int active = nbt_fs_exists(fs, NBT_FS_ACTIVE, app, NULL);
    const int backup = nbt_fs_exists(fs, NBT_FS_BACKUP, app, j.tx());
    const int candidate = nbt_fs_exists(fs, NBT_FS_CANDIDATE, app, j.tx());
    if (active < 0 || backup < 0 || candidate < 0) {
        return errorOut(error, errorSize, "unsafe transaction files");
    }
    if (!strcmp(j.phase(), "CONFIRMED")) {
        if (!active || !backup || fileHash(fs, NBT_FS_ACTIVE, app, NULL, hash) ||
            strcmp(hash, j.newRecord().sha256) || fileHash(fs, NBT_FS_BACKUP, app, j.tx(), hash) ||
            strcmp(hash, j.oldRecord().sha256) ||
            InstalledRecord::writeRecord(fs, app, &j.newRecord())) {
            return errorOut(error, errorSize, "confirmed transaction cannot be recovered");
        }
    } else if (backup) {
        if (fileHash(fs, NBT_FS_BACKUP, app, j.tx(), hash) || strcmp(hash, j.oldRecord().sha256)) {
            return errorOut(error, errorSize, "previous executable is invalid");
        }
        if (active &&
            (fileHash(fs, NBT_FS_ACTIVE, app, NULL, hash) || strcmp(hash, j.newRecord().sha256))) {
            return errorOut(error, errorSize, "unexpected installed executable");
        }
        if (nbt_fs_rename(fs, NBT_FS_BACKUP, NBT_FS_ACTIVE, app, j.tx()) ||
            nbt_fs_barrier(fs, app, j.tx())) {
            return errorOut(error, errorSize, "cannot restore previous executable");
        }
        NBT_FAULT_POINT("NBT_TEST_FAULT", "recovery-restored");
    } else if (!active || fileHash(fs, NBT_FS_ACTIVE, app, NULL, hash) ||
               strcmp(hash, j.oldRecord().sha256)) {
        return errorOut(error, errorSize, "previous executable unavailable");
    }
    if (candidate && nbt_fs_unlink(fs, NBT_FS_CANDIDATE, app, j.tx())) {
        return errorOut(error, errorSize, "cannot remove staged executable");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "recovery-cleaned");
    if (nbt_fs_unlink(fs, NBT_FS_JOURNAL, app, NULL) || nbt_fs_barrier(fs, app, j.tx())) {
        return errorOut(error, errorSize, "cannot finish recovery");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "recovery-finished");
    if (errorSize) {
        *error = 0;
    }
    return 0;
}

#ifdef NBT_TESTING
core::Status TransactionJournal::parseForTesting(core::Span<const char> text) {
    char copy[1024 + 2 * NBT_BRIDGE_TEXT_MAX];
    TransactionJournal parsed;
    if (!core::copyTerminated(core::StringRef(text.data(), text.size()),
                              core::Span<char>(copy, sizeof(copy)))
             .ok()) {
        return core::Status::error(EINVAL, "invalid transaction journal size");
    }
    return parsed.parse(copy) ? core::Status::error(EINVAL, "invalid transaction journal")
                              : core::Status();
}
#endif

} // namespace lifecycle
} // namespace nbt

int nbt_tx_candidate_bridge(nbt_fs *fs, const char *app, nbt_bridge_requirement *out) {
    nbt::lifecycle::TransactionJournal j;
    if (!out || j.load(fs, app) || strcmp(j.phase(), "INSTALLED")) {
        return -1;
    }
    *out = j.newRecord().bridge;
    return 0;
}

int nbt_tx_confirm(nbt_fs *fs, const char *app, char *error, unsigned error_size) {
    return nbt::lifecycle::TransactionJournal::confirm(fs, app, error, error_size);
}

int nbt_tx_recover(nbt_fs *fs, const char *app, char *error, unsigned error_size) {
    return nbt::lifecycle::TransactionJournal::recover(fs, app, error, error_size);
}
