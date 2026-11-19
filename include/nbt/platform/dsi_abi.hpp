// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PLATFORM_DSI_ABI_HPP
#define NBT_PLATFORM_DSI_ABI_HPP

namespace nbt {
namespace platform {
namespace dsi_abi {

/* ARM32 GNU ABI layout observed in libsys_dsi_colibry.gpp.so.8
 * (SHA-256 f559c5c1455ed62e376ec72fdb77d5211b748bc9167f9c6866083db1515d19b4).
 * Allocations include margin; field offsets are exact. See
 * docs/head-unit-interfaces.md for the functions that establish them. */
enum {
    kConnectionBytes = 0x8c + 64,
    kClientBytes = 0x14c + 64,
    kStreamBytes = 0xbc + 64,
    kClientParty = 0x110,
    kServerParty = 0x118,
    kClientChannel = 0x130,
    kClientSocket = 0x138,
    kClientIpcFd = 0x140,
    kInputStreamLength = 0x08,
    kInputStreamPayload = 0x10,
    kInputStreamId = 0x48,
    kVtableSlots = 8,
    kSlotAttached = 3,
    kSlotDetached = 4,
    kSlotData = 7
};

} // namespace dsi_abi
} // namespace platform
} // namespace nbt

#endif
