// SPDX-License-Identifier: MPL-2.0
#include "nbt/most/input_trace.hpp"

#include "nbt/log.h"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace most {

InputTrace::InputTrace() : head_(0), count_(0) {
    memset(entries_, 0, sizeof(entries_));
}

void InputTrace::record(uint64_t nowMs, unsigned fblock, unsigned inst, unsigned fkt, unsigned op,
                        const unsigned char *data, size_t length, size_t inputs) {
    core::LockGuard guard(lock_);
    Entry &entry = entries_[(head_ + count_) % kTraceEntries];
    if (count_ < kTraceEntries) {
        ++count_;
    } else {
        head_ = (head_ + 1) % kTraceEntries; // the oldest goes
    }
    entry.ms = nowMs;
    entry.fblock = static_cast<uint8_t>(fblock);
    entry.inst = static_cast<uint8_t>(inst);
    entry.fkt = static_cast<uint16_t>(fkt);
    entry.op = static_cast<uint8_t>(op);
    entry.length = static_cast<uint8_t>(length > 255 ? 255 : length);
    entry.inputs = static_cast<uint8_t>(inputs > 255 ? 255 : inputs);
    memset(entry.data, 0, sizeof(entry.data));
    if (data && length) {
        memcpy(entry.data, data, length < kTraceBytes ? length : kTraceBytes);
    }
}

bool InputTrace::formatLocked(size_t index, uint64_t nowMs, char *out, size_t size) const {
    if (index >= count_ || !out || !size) {
        return false;
    }
    const Entry &entry = entries_[(head_ + index) % kTraceEntries];
    const long ago = nowMs >= entry.ms ? static_cast<long>(nowMs - entry.ms) : 0L;
    int used = snprintf(out, size, "-%ld ms %02X.%02X fkt %03X op %X len %u:", ago, entry.fblock,
                        entry.inst, entry.fkt, entry.op, entry.length);
    const size_t shown = entry.length < kTraceBytes ? entry.length : kTraceBytes;
    for (size_t i = 0; i < shown && used > 0 && static_cast<size_t>(used) < size; ++i) {
        used += snprintf(out + used, size - static_cast<size_t>(used), " %02X", entry.data[i]);
    }
    if (used > 0 && static_cast<size_t>(used) < size) {
        snprintf(out + used, size - static_cast<size_t>(used), "%s -> %u",
                 entry.length > kTraceBytes ? " .." : "", entry.inputs);
    }
    return true;
}

bool InputTrace::format(size_t index, uint64_t nowMs, char *out, size_t size) {
    core::LockGuard guard(lock_);
    return formatLocked(index, nowMs, out, size);
}

size_t InputTrace::dump(const char *why, uint64_t nowMs) {
    core::LockGuard guard(lock_);
    nbt_logf("controller messages before %s: %u", why ? why : "?", static_cast<unsigned>(count_));
    char line[120];
    for (size_t i = 0; formatLocked(i, nowMs, line, sizeof(line)); ++i) {
        nbt_logf("  %s", line);
    }
    const size_t logged = count_;
    head_ = 0;
    count_ = 0;
    return logged;
}

} // namespace most
} // namespace nbt
