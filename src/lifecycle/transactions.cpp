// SPDX-License-Identifier: MPL-2.0
#include "nbt/lifecycle/transactions.hpp"

#include "nbt/core/fault.hpp"
#include "nbt/lifecycle/transaction_journal.hpp"
#include "nbt/manager_state.h"
#include "nbt/model/installed_record.hpp"
#include "nbt/signers.h"
#include "nbt/storage/path_open.hpp"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

using nbt::lifecycle::TransactionJournal;
using nbt::model::InstalledRecord;

// An update is signed by the key that signed the application's first package
// (nbt/signers.h): the apps key bundled with the manager, or the developer key
// its profile names. The profile also fixes the application's compatibility id.
int verifyUploadFd(nbt_fs *fs, const char *app, int upload, nbt_package *package, char *error,
                   unsigned errorSize) {
    char compat[NBT_ID_MAX + 1], signer[NBT_SIGNER_MAX + 1];
    if (InstalledRecord::profileRead(fs, app, compat, signer)) {
        return TransactionJournal::errorOut(error, errorSize, "invalid application profile");
    }
    const int key = nbt_signer_key_fd(fs, signer);
    if (key < 0) {
        return TransactionJournal::errorOut(error, errorSize,
                                            signer[0] ? "signing key removed from this car"
                                                      : "no apps key in this manager");
    }
    const bool ok = !nbt_package_verify(upload, key, app, compat, package, error, errorSize);
    close(key);
    if (!ok) {
        return -1;
    }
    nbt_bridge_provider provider;
    nbt_bridge_current(&provider);
    return nbt_bridge_check(&package->bridge, &provider, NULL, NULL, error, errorSize);
}

int beginLocked(nbt_fs *fs, const char *app, const unsigned char approvedHash[32], int recovery,
                nbt_package *package, char *error, unsigned errorSize) {
    char oldHash[65];
    nbt_installed oldRecord;
    TransactionJournal journal;
    struct statvfs space;
    if (nbt_fs_exists(fs, NBT_FS_JOURNAL, app, NULL) != 0 ||
        nbt_fs_exists(fs, NBT_FS_JOURNAL_NEXT, app, NULL) != 0 ||
        nbt_fs_exists(fs, NBT_FS_RECORD_NEXT, app, NULL) != 0) {
        return TransactionJournal::errorOut(error, errorSize, "recovery required");
    }
    if (nbt_tx_installed(fs, app, &oldRecord) ||
        TransactionJournal::fileHash(fs, NBT_FS_ACTIVE, app, NULL, oldHash) ||
        strcmp(oldHash, oldRecord.sha256)) {
        return TransactionJournal::errorOut(error, errorSize,
                                            "invalid profile or installed baseline");
    }
    const int upload = nbt_fs_open(fs, NBT_FS_UPLOAD, app, NULL, NBT_FS_READ);
    if (upload < 0) {
        return TransactionJournal::errorOut(error, errorSize, "no staged package");
    }
    if (verifyUploadFd(fs, app, upload, package, error, errorSize)) {
        close(upload);
        return -1;
    }
    if (memcmp(package->package_hash, approvedHash, 32)) {
        close(upload);
        return TransactionJournal::errorOut(error, errorSize, "approved package changed");
    }
    if ((!recovery && package->sequence <= oldRecord.highwater) ||
        (recovery && package->sequence >= oldRecord.sequence)) {
        close(upload);
        return TransactionJournal::errorOut(error, errorSize, "release sequence rejected");
    }
    if (statvfs(fs->root, &space) || !space.f_frsize ||
        space.f_bavail < (package->payload_size / space.f_frsize + 2)) {
        close(upload);
        return TransactionJournal::errorOut(error, errorSize, "insufficient staging space");
    }
    journal.reset(oldRecord, *package);
    if (nbt_hex(package->payload_hash, 32, journal.newRecordMutable().sha256,
                sizeof(journal.newRecordMutable().sha256))) {
        close(upload);
        return TransactionJournal::errorOut(error, errorSize, "invalid payload digest");
    }
    if (nbt_fs_random_tx(journal.txMutable()) || nbt_fs_create_tx(fs, app, journal.tx()) ||
        InstalledRecord::copyCandidate(fs, app, journal.tx(), upload, package)) {
        close(upload);
        return TransactionJournal::errorOut(error, errorSize, "cannot stage executable");
    }
    close(upload);
    NBT_FAULT_POINT("NBT_TEST_FAULT", "candidate");
    if (journal.writePhase(fs, app, "PREPARED")) {
        return TransactionJournal::errorOut(error, errorSize, "cannot persist transaction");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "prepared");
    if (nbt_fs_rename(fs, NBT_FS_ACTIVE, NBT_FS_BACKUP, app, journal.tx()) ||
        nbt_fs_barrier(fs, app, journal.tx())) {
        return TransactionJournal::errorOut(error, errorSize, "cannot retain previous executable");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "backed-up-rename");
    if (journal.writePhase(fs, app, "BACKED_UP")) {
        return TransactionJournal::errorOut(error, errorSize, "cannot persist backup state");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "backed-up");
    if (nbt_fs_rename(fs, NBT_FS_CANDIDATE, NBT_FS_ACTIVE, app, journal.tx()) ||
        nbt_fs_barrier(fs, app, journal.tx())) {
        return TransactionJournal::errorOut(error, errorSize, "cannot install executable");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "installed-rename");
    if (journal.writePhase(fs, app, "INSTALLED")) {
        return TransactionJournal::errorOut(error, errorSize, "cannot persist installed state");
    }
    NBT_FAULT_POINT("NBT_TEST_FAULT", "installed");
    if (errorSize) {
        *error = 0;
    }
    return 0;
}

} // namespace

int nbt_tx_lock(nbt_fs *fs, const char *app) {
    return nbt::storage::SafePathOpen::lockNonBlocking(fs, NBT_FS_LOCK, app);
}

int nbt_tx_manager_lock(nbt_fs *fs) {
    char text[NBT_MANAGER_STATE_MAX];
    char path[NBT_PATH_MAX];
    nbt_manager_state state;
    struct stat st;
    if (nbt_fs_path(fs, NBT_FS_STATE_DIR, "manager", NULL, path, sizeof(path))) {
        return -1;
    }
    if (lstat(path, &st)) {
        return errno == ENOENT ? -2 : -1;
    }
    int lock = nbt_fs_manager_operation_lock(fs);
    if (lock < 0) {
        lock = nbt_fs_manager_operation_parent(fs); // the worker under its launcher
    }
    if (lock < 0) {
        errno = EAGAIN; // another process holds it: App Manager is open, or another operation
        return -1;
    }
    const int present = nbt_fs_exists(fs, NBT_FS_MANAGER_STATE, "manager", NULL);
    if (present < 0 || nbt_fs_exists(fs, NBT_FS_MANAGER_STATE_NEXT, "manager", NULL) != 0 ||
        (present &&
         (InstalledRecord::readText(fs, NBT_FS_MANAGER_STATE, "manager", text, sizeof(text)) ||
          nbt_manager_state_parse(text, strlen(text), &state) || state.pending))) {
        close(lock);
        errno = EBUSY; // a manager release waits for its trial's confirmation
        return -1;
    }
    return lock;
}

int nbt_tx_verify(nbt_fs *fs, const char *app, nbt_package *package, char *error,
                  unsigned error_size) {
    const int upload = nbt_fs_open(fs, NBT_FS_UPLOAD, app, NULL, NBT_FS_READ);
    if (upload < 0) {
        return TransactionJournal::errorOut(error, error_size, "no staged package");
    }
    const int result = verifyUploadFd(fs, app, upload, package, error, error_size);
    close(upload);
    return result;
}

int nbt_tx_begin(nbt_fs *fs, const char *app, const unsigned char approved_hash[32], int recovery,
                 nbt_package *package, char *error, unsigned error_size) {
    const int lock = nbt_tx_manager_lock(fs);
    if (lock == -1) {
        return TransactionJournal::errorOut(error, error_size,
                                            "manager update or recovery in progress");
    }
    const int result = beginLocked(fs, app, approved_hash, recovery, package, error, error_size);
    if (lock >= 0) {
        close(lock);
    }
    return result;
}

namespace nbt {
namespace lifecycle {
namespace {

core::Status operationFailed(const char *message) {
    return core::Status::error(errno ? errno : EIO, message);
}

} // namespace

core::Result<storage::AppTxLock> AppTransactions::lock(const storage::ManagedRoot &root,
                                                       const char *app) {
    core::Result<core::UniqueFd> fd = root.open(NBT_FS_LOCK, app, NULL, NBT_FS_OPEN_LOCK);
    if (!fd.ok()) {
        return core::Result<storage::AppTxLock>::failure(fd.status());
    }
    return storage::AppTxLock::acquire(std::move(fd.value()));
}

core::Result<nbt_installed> AppTransactions::installed(const storage::ManagedRoot &root,
                                                       const char *app) {
    nbt_installed record;
    memset(&record, 0, sizeof(record));
    if (!root.valid() || nbt_tx_installed(const_cast<nbt_fs *>(root.legacy()), app, &record)) {
        return core::Result<nbt_installed>::failure(
            operationFailed("installed record load failed"));
    }
    return core::Result<nbt_installed>::success(record);
}

core::Status AppTransactions::verify(const storage::AppTxLock &lock,
                                     const storage::ManagedRoot &root, const char *app,
                                     nbt_package *package) {
    (void)&lock;
    char error[256];
    if (!root.valid() || !package) {
        return core::Status::error(EINVAL, "invalid transaction arguments");
    }
    if (nbt_tx_verify(const_cast<nbt_fs *>(root.legacy()), app, package, error, sizeof(error))) {
        return operationFailed("package verification failed");
    }
    return core::Status();
}

core::Status AppTransactions::begin(const storage::AppTxLock &lock,
                                    const storage::ManagedRoot &root, const char *app,
                                    const unsigned char approvedHash[32], bool recovery,
                                    nbt_package *package) {
    (void)&lock;
    char error[256];
    if (!root.valid() || !approvedHash || !package) {
        return core::Status::error(EINVAL, "invalid transaction arguments");
    }
    if (nbt_tx_begin(const_cast<nbt_fs *>(root.legacy()), app, approvedHash, recovery ? 1 : 0,
                     package, error, sizeof(error))) {
        return operationFailed("application transaction failed");
    }
    return core::Status();
}

core::Status AppTransactions::confirm(const storage::AppTxLock &lock,
                                      const storage::ManagedRoot &root, const char *app) {
    (void)&lock;
    char error[256];
    if (!root.valid() ||
        nbt_tx_confirm(const_cast<nbt_fs *>(root.legacy()), app, error, sizeof(error))) {
        return operationFailed("transaction confirmation failed");
    }
    return core::Status();
}

core::Status AppTransactions::recover(const storage::AppTxLock &lock,
                                      const storage::ManagedRoot &root, const char *app) {
    (void)&lock;
    char error[256];
    if (!root.valid() ||
        nbt_tx_recover(const_cast<nbt_fs *>(root.legacy()), app, error, sizeof(error))) {
        return operationFailed("transaction recovery failed");
    }
    return core::Status();
}

core::Result<nbt_bridge_requirement>
AppTransactions::candidateBridge(const storage::AppTxLock &lock, const storage::ManagedRoot &root,
                                 const char *app) {
    (void)&lock;
    nbt_bridge_requirement bridge;
    memset(&bridge, 0, sizeof(bridge));
    if (!root.valid() ||
        nbt_tx_candidate_bridge(const_cast<nbt_fs *>(root.legacy()), app, &bridge)) {
        return core::Result<nbt_bridge_requirement>::failure(
            operationFailed("candidate bridge unavailable"));
    }
    return core::Result<nbt_bridge_requirement>::success(bridge);
}

#ifdef NBT_TESTING
core::Status AppTransactions::parseJournalForTesting(core::Span<const char> text) {
    return TransactionJournal::parseForTesting(text);
}
#endif

} // namespace lifecycle
} // namespace nbt
