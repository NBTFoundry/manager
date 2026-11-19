// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_UPDATE_RECEIVER_HPP
#define NBT_LAUNCHER_UPDATE_RECEIVER_HPP

#include "nbt/core/status.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace launcher {

class UpdateReceiver {
public:
    explicit UpdateReceiver(const storage::ManagedRoot &root) : root_(root) {
    }

    core::Status receive(int inputFd, uint64_t size, const unsigned char expectedHash[32],
                         bool recovery, char *error, unsigned errorSize) const;

private:
    const storage::ManagedRoot &root_;
};

} // namespace launcher
} // namespace nbt

#endif
