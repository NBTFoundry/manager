// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MOST_INPUT_TRACE_HPP
#define NBT_MOST_INPUT_TRACE_HPP

// The last controller messages the GUI's MOST client received, raw, with how
// many inputs each decoded to. Written to the debug log when iDrive takes the
// screen from the manager or an application, to show what came just before
// (a BACK, a fast knob turn, a hard key the decoder ignores). In memory only;
// thread safe (recorded on the DSI thread, written from the GUI's).

#include "nbt/core/mutex.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace most {

static const size_t kTraceEntries = 48;
static const size_t kTraceBytes = 8;

class InputTrace {
public:
    InputTrace();
    void record(uint64_t nowMs, unsigned fblock, unsigned inst, unsigned fkt, unsigned op,
                const unsigned char *data, size_t length, size_t inputs);
    // Logs the recorded messages, oldest first, and forgets them; the count logged.
    size_t dump(const char *why, uint64_t nowMs);
    // One recorded message as logged ("-120 ms 05.00 fkt C46 op C: 01 04 -> 1"),
    // oldest first; false past the last.
    bool format(size_t index, uint64_t nowMs, char *out, size_t size);

private:
    struct Entry {
        uint64_t ms;
        uint16_t fkt;
        uint8_t fblock, inst, op, length, inputs;
        uint8_t data[kTraceBytes];
    };
    bool formatLocked(size_t index, uint64_t nowMs, char *out, size_t size) const;

    core::Mutex lock_;
    Entry entries_[kTraceEntries];
    size_t head_;
    size_t count_;
};

} // namespace most
} // namespace nbt

#endif
