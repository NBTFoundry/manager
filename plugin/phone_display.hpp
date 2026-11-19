// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PLUGIN_PHONE_DISPLAY_HPP
#define NBT_PLUGIN_PHONE_DISPLAY_HPP
#include <stddef.h>
#include <stdint.h>
namespace nbt {
namespace plugin {
// Trivial storage: no startup constructor or allocation in the HMI process.
struct PhoneDisplayProbe {
    int fd;
    int pid;
    uint64_t started;
    size_t used;
    char output[1024];
    void reset();
    void stop();
    bool start(uint64_t now);
    int poll(uint64_t now); // -1 pending/unknown, 0 busy, 1 iDrive alone
};
} // namespace plugin
} // namespace nbt
#endif
