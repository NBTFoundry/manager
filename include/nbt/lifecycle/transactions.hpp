#ifndef NBT_LIFECYCLE_TRANSACTIONS_HPP
#define NBT_LIFECYCLE_TRANSACTIONS_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/storage/locks.hpp"
#include "nbt/storage/managed_root.hpp"
#include "nbt/transaction.h"

namespace nbt {
namespace lifecycle {

class AppTransactions {
public:
    static core::Result<storage::AppTxLock> lock(const storage::ManagedRoot &root, const char *app);
    static core::Result<nbt_installed> installed(const storage::ManagedRoot &root, const char *app);
    static core::Status verify(const storage::AppTxLock &lock, const storage::ManagedRoot &root,
                               const char *app, nbt_package *package);
    static core::Status begin(const storage::AppTxLock &lock, const storage::ManagedRoot &root,
                              const char *app, const unsigned char approvedHash[32], bool recovery,
                              nbt_package *package);
    static core::Status confirm(const storage::AppTxLock &lock, const storage::ManagedRoot &root,
                                const char *app);
    static core::Status recover(const storage::AppTxLock &lock, const storage::ManagedRoot &root,
                                const char *app);
    static core::Result<nbt_bridge_requirement> candidateBridge(const storage::AppTxLock &lock,
                                                                const storage::ManagedRoot &root,
                                                                const char *app);
#ifdef NBT_TESTING
    static core::Status parseJournalForTesting(core::Span<const char> text);
#endif
};

} // namespace lifecycle
} // namespace nbt

#endif
