// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_MAPPED_CHANNELS_HPP
#define NBT_PROCESS_MAPPED_CHANNELS_HPP
#include "nbt/core/unique_fd.hpp"
namespace nbt {
namespace process {
/* Private copies survive spawn's descriptor mapping; all paths close them. */
class MappedChannels {
public:
    MappedChannels(int status, int most, int gamepad, int controller, int output, int hold = -1,
                   int diag = -1, int companion = -1);
    bool valid() const {
        return valid_;
    }
    int status() const {
        return fds_[0].get();
    }
    int most() const {
        return fds_[1].get();
    }
    int gamepad() const {
        return fds_[2].get();
    }
    int controller() const {
        return fds_[3].get();
    }
    int output() const {
        return fds_[4].get();
    }
    int hold() const {
        return fds_[5].get();
    }
    int diag() const {
        return fds_[6].get();
    }
    int companion() const {
        return fds_[7].get();
    }

private:
    core::UniqueFd fds_[8];
    bool valid_;
};
} // namespace process
} // namespace nbt
#endif
