// SPDX-License-Identifier: MPL-2.0
/* The rules are the SDK's (src/payload.c, tools/nbt_elf.py): the module
 * runtime writes the note, nbt-runner reads it again before running. */
#include "nbt/package/payload_note.hpp"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace package {
namespace {

const size_t kReadMax = 64u * 1024u;    // notes sit near the start
const uint32_t kNoteName = 0x0054424eu; // "NBT\0"

uint16_t rd16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t rd32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/* The NBT note in a PT_NOTE segment [off, off + len) of `d`. */
bool noteIn(const uint8_t *d, uint32_t off, uint32_t len, PayloadNote *out) {
    uint32_t pos = 0;
    while (len - pos >= 12) {
        const uint8_t *n = d + off + pos;
        const uint32_t namesz = rd32(n), descsz = rd32(n + 4), type = rd32(n + 8);
        if (namesz > 64 || descsz > 1024) {
            return false;
        }
        const uint32_t nameRoom = (namesz + 3u) & ~3u, descRoom = (descsz + 3u) & ~3u;
        if (nameRoom + descRoom > len - pos - 12) {
            return false;
        }
        if (namesz == 4 && rd32(n + 12) == kNoteName && type == 1 && descsz >= 12) {
            const uint8_t *desc = n + 12 + nameRoom;
            out->abiMajor = rd32(desc);
            out->abiMinor = rd32(desc + 4);
            out->mode = rd32(desc + 8);
            return true;
        }
        pos += 12 + nameRoom + descRoom;
    }
    return false;
}

} // namespace

bool parsePayloadNote(const uint8_t *d, size_t size, PayloadNote *out) {
    if (!d || !out || size < 52 || memcmp(d, "\177ELF", 4) || d[4] != 1 || d[5] != 1 ||
        rd16(d + 18) != 40) {
        return false;
    }
    const uint16_t type = rd16(d + 16);
    const uint32_t phoff = rd32(d + 28);
    const uint16_t phentsize = rd16(d + 42), phnum = rd16(d + 44);
    if (phentsize < 32 || !phnum || phnum > 64 || phoff > size ||
        static_cast<size_t>(phnum) * phentsize > size - phoff) {
        return false;
    }
    for (uint16_t i = 0; i < phnum; ++i) {
        const uint8_t *ph = d + phoff + static_cast<size_t>(i) * phentsize;
        const uint32_t off = rd32(ph + 4), len = rd32(ph + 16);
        if (rd32(ph) != 4 /* PT_NOTE */ || off > size || len > size - off) {
            continue;
        }
        PayloadNote note;
        if (noteIn(d, off, len, &note)) {
            if ((note.mode == kPayloadModule && type != 3) ||
                (note.mode == kPayloadGuest && type != 2) ||
                (note.mode != kPayloadModule && note.mode != kPayloadGuest)) {
                return false;
            }
            *out = note;
            return true;
        }
    }
    return false;
}

bool readPayloadNote(int fd, PayloadNote *out) {
    uint8_t *buffer = static_cast<uint8_t *>(malloc(kReadMax));
    if (!buffer) {
        return false;
    }
    size_t got = 0;
    while (got < kReadMax) {
        const ssize_t n = pread(fd, buffer + got, kReadMax - got, static_cast<off_t>(got));
        if (n <= 0) {
            break;
        }
        got += static_cast<size_t>(n);
    }
    const bool found = parsePayloadNote(buffer, got, out);
    free(buffer);
    return found;
}

} // namespace package
} // namespace nbt
