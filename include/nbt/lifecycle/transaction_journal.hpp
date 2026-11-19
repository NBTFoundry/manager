#ifndef NBT_LIFECYCLE_TRANSACTION_JOURNAL_HPP
#define NBT_LIFECYCLE_TRANSACTION_JOURNAL_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/fs.h"
#include "nbt/transaction.h"

namespace nbt {
namespace lifecycle {

class TransactionJournal {
public:
    TransactionJournal();

    int parse(char *text);
    int load(nbt_fs *fs, const char *app);
    int writePhase(nbt_fs *fs, const char *app, const char *phase);

    void reset(const nbt_installed &oldRecord, const nbt_package &package);
    char *txMutable() {
        return tx_;
    }
    const char *tx() const {
        return tx_;
    }
    const char *phase() const {
        return phase_;
    }
    const nbt_installed &oldRecord() const {
        return oldRecord_;
    }
    const nbt_installed &newRecord() const {
        return newRecord_;
    }
    nbt_installed &newRecordMutable() {
        return newRecord_;
    }

    static int fileHash(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx,
                        char out[65]);
    static int errorOut(char *error, unsigned size, const char *message);
    static int confirm(nbt_fs *fs, const char *app, char *error, unsigned errorSize);
    static int recover(nbt_fs *fs, const char *app, char *error, unsigned errorSize);

#ifdef NBT_TESTING
    static core::Status parseForTesting(core::Span<const char> text);
#endif

private:
    char tx_[33];
    char phase_[16];
    nbt_installed oldRecord_;
    nbt_installed newRecord_;
};

} // namespace lifecycle
} // namespace nbt

#endif
