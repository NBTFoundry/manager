// SPDX-License-Identifier: MPL-2.0
/* The tester's DoIP side (ISO 13400-2, docs/engine-diagnostics.md): an
 * 8-byte header (version 0x02, its inverse 0xFD, a 2-byte payload type, a
 * 4-byte length), a routing activation per connection (0x0005, answered
 * 0x0006 with code 0x10 when activated), then diagnostic messages (0x8001:
 * source, target, UDS) the gateway acknowledges (0x8002, or 0x8003 refused)
 * before the control unit's answer. It may ask whether the tester is still
 * there (0x0007, answered 0x0008). */
#include "nbt/diag/hsfz_tester.hpp"

#include "nbt/util.h"

#include <errno.h>
#include <string.h>

namespace nbt {
namespace diag {
namespace {

enum {
    kHeader = 8,
    kGenericNack = 0x0000,
    kActivationRequest = 0x0005,
    kActivationResponse = 0x0006,
    kAliveRequest = 0x0007,
    kAliveResponse = 0x0008,
    kDiagMessage = 0x8001,
    kDiagNack = 0x8003,
    kActivated = 0x10,
};

int remaining(uint64_t deadline) {
    uint64_t now = 0;
    if (nbt_monotonic_ms(&now) || now >= deadline) {
        return 0;
    }
    return static_cast<int>(deadline - now);
}

uint64_t after(int ms) {
    uint64_t now = 0;
    (void)nbt_monotonic_ms(&now);
    return now + static_cast<uint64_t>(ms > 0 ? ms : 0);
}

uint16_t word(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] << 8 | p[1]);
}

void putWord(uint8_t *p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value >> 8);
    p[1] = static_cast<uint8_t>(value);
}

} // namespace

bool HsfzTester::sendDoip(uint16_t type, const uint8_t *payload, size_t size) {
    uint8_t frame[kHsfzFrameMax];
    if (size + kHeader > sizeof(frame)) {
        return false;
    }
    frame[0] = 0x02;
    frame[1] = 0xFD;
    putWord(frame + 2, type);
    frame[4] = static_cast<uint8_t>(size >> 24);
    frame[5] = static_cast<uint8_t>(size >> 16);
    frame[6] = static_cast<uint8_t>(size >> 8);
    frame[7] = static_cast<uint8_t>(size);
    if (size) {
        memcpy(frame + kHeader, payload, size);
    }
    return sendAll(frame, kHeader + size);
}

bool HsfzTester::nextDoipFrame(uint16_t *type, const uint8_t **payload, size_t *size) {
    const size_t available = length_ - consumed_;
    if (available < kHeader) {
        return false;
    }
    const uint8_t *p = buffer_ + consumed_;
    const uint32_t length = static_cast<uint32_t>(p[4]) << 24 | static_cast<uint32_t>(p[5]) << 16 |
                            static_cast<uint32_t>(p[6]) << 8 | p[7];
    if (length > kHsfzFrameMax - kHeader || available < kHeader + length) {
        return false;
    }
    *type = word(p + 2);
    *payload = p + kHeader;
    *size = length;
    consumed_ += kHeader + length;
    return true;
}

bool HsfzTester::activate(int timeoutMs) {
    uint8_t request[7] = {0, 0, 0x00, 0, 0, 0, 0}; // source, default activation, reserved
    putWord(request, doipTester_);
    if (!sendDoip(kActivationRequest, request, sizeof(request))) {
        return false;
    }
    const uint64_t deadline = after(timeoutMs);
    for (;;) {
        uint16_t type = 0;
        const uint8_t *payload = NULL;
        size_t length = 0;
        while (nextDoipFrame(&type, &payload, &length)) {
            if (type == kActivationResponse && length >= 5) {
                activation_ = payload[4];
                return activation_ == kActivated;
            }
            if (type == kGenericNack) {
                activation_ = 0x100;
                return false;
            }
        }
        const int left = remaining(deadline);
        if (!left || !fill(left)) {
            return false;
        }
    }
}

HsfzTester::Result HsfzTester::requestDoip(uint8_t ecu, const uint8_t *uds, size_t size,
                                           uint8_t *answer, size_t capacity, size_t *answerSize,
                                           int timeoutMs) {
    uint8_t message[kHsfzFrameMax - kHeader];
    if (fd_ < 0 || !uds || !size || size + 4 > sizeof(message)) {
        return Broken;
    }
    putWord(message, doipTester_);
    putWord(message + 2, ecu);
    memcpy(message + 4, uds, size);
    if (!sendDoip(kDiagMessage, message, size + 4)) {
        return Broken;
    }
    uint64_t deadline = after(timeoutMs);
    int pending = 0;
    for (;;) {
        uint16_t type = 0;
        const uint8_t *payload = NULL;
        size_t length = 0;
        while (nextDoipFrame(&type, &payload, &length)) {
            if (type == kAliveRequest) {
                uint8_t reply[2];
                putWord(reply, doipTester_);
                if (!sendDoip(kAliveResponse, reply, sizeof(reply))) {
                    return Broken;
                }
            } else if (type == kGenericNack) {
                refusal_ = static_cast<uint16_t>(0x0100 | (length ? payload[0] : 0));
                return Refused;
            } else if (type == kDiagNack && length >= 5) {
                refusal_ = static_cast<uint16_t>(0x8300 | payload[4]);
                return Refused;
            } else if (type == kDiagMessage && length >= 5 && word(payload) == ecu &&
                       word(payload + 2) == doipTester_) {
                const uint8_t *reply = payload + 4;
                const size_t n = length - 4;
                if (n >= 3 && reply[0] == 0x7F && reply[1] == uds[0] && reply[2] == 0x78 &&
                    pending < 5) {
                    ++pending;
                    deadline = after(timeoutMs);
                    continue;
                }
                if (reply[0] == (uds[0] | 0x40) ||
                    (n >= 2 && reply[0] == 0x7F && reply[1] == uds[0])) {
                    const size_t copy = n < capacity ? n : capacity;
                    memcpy(answer, reply, copy);
                    *answerSize = copy;
                    return Ok;
                }
            }
            // Positive acknowledgements (0x8002) and anything else: skipped.
        }
        const int left = remaining(deadline);
        if (!left) {
            return Timeout;
        }
        if (!fill(left)) {
            return Broken;
        }
    }
}

} // namespace diag
} // namespace nbt
