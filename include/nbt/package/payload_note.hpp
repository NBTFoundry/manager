// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PACKAGE_PAYLOAD_NOTE_HPP
#define NBT_PACKAGE_PAYLOAD_NOTE_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace package {

/* An application built without the QNX SDP: its payload carries the SDK's NBT
 * ABI note (.note.nbt, docs/app-bridge.md "runner") and nbt-runner runs it. */
enum PayloadMode { kPayloadModule = 1, kPayloadGuest = 2 };

struct PayloadNote {
    uint32_t abiMajor;
    uint32_t abiMinor;
    uint32_t mode; // PayloadMode
};

/* The note of an ELF32 little-endian ARM image, with the mode matching the ELF
 * type (a module is ET_DYN, a guest ET_EXEC). Every offset is checked: the
 * image is the application's. */
bool parsePayloadNote(const uint8_t *data, size_t size, PayloadNote *out);

/* The same on the first 64 KiB of `fd` (pread: the offset is kept). */
bool readPayloadNote(int fd, PayloadNote *out);

} // namespace package
} // namespace nbt

#endif
