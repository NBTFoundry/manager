// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_SLOT_STATE_HPP
#define NBT_LAUNCHER_SLOT_STATE_HPP

#include "nbt/core/status.hpp"
#include "nbt/manager_state.h"

namespace nbt {
namespace launcher {

enum class RestartDecision { Restart, Exhausted };

class SlotState {
public:
    SlotState();

    static core::Result<SlotState> initial(const nbt_release &release);
    static core::Result<SlotState> fromLegacy(const nbt_manager_state &state);
    core::Status begin(const nbt_release &release, bool recovery);
    core::Status trial();
    core::Status rollback();
    core::Status confirm(const nbt_process &process, uint64_t nowMs);
    core::Status parse(const void *data, size_t size);
    core::Result<size_t> format(char *buffer, size_t size) const;
    RestartDecision recordRestart(uint64_t epochSeconds, unsigned *delayMs);
    void clearRestarts() {
        state_.restarts = 0;
        state_.restart_epoch = 0;
    }

    const nbt_manager_state &legacy() const {
        return state_;
    }

private:
    nbt_manager_state state_;
};

} // namespace launcher
} // namespace nbt

#endif
