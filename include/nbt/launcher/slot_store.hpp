#ifndef NBT_LAUNCHER_SLOT_STORE_HPP
#define NBT_LAUNCHER_SLOT_STORE_HPP

#include "nbt/core/status.hpp"
#include "nbt/manager_store.h"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace launcher {

class SlotStore {
public:
    explicit SlotStore(const storage::ManagedRoot &root) : root_(root) {
    }

    core::Status load(nbt_manager_state &state) const;
    core::Status initial(const nbt_release &release) const;
    core::Status resumeInitial(const nbt_release &release) const;
    core::Status begin(const nbt_release &release, bool recovery) const;
    core::Status trial() const;
    core::Status rollback() const;
    core::Status confirm(const nbt_process &process, uint64_t nowMs) const;
    core::Result<unsigned> recordRestart(uint64_t epochSeconds) const;
    core::Status clearRestarts() const;

private:
    const storage::ManagedRoot &root_;
};

} // namespace launcher
} // namespace nbt

#endif
