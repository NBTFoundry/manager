// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_TOUCH_INPUT_HPP
#define NBT_GUI_TOUCH_INPUT_HPP

#include "nbt/core/unique_fd.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace gui {

/* The head unit's touch screen (docs/head-unit-interfaces.md, "The touch
 * screen"): the touch controller sends UDP datagrams to port 30567, where
 * iDrive listens; a socket bound there later takes them all. The manager binds
 * it only while its own screen is on the panel, so iDrive has its touches
 * back whenever iDrive is shown, and at once when the GUI ends. */
static const uint16_t kTouchPort = 30567;
static const size_t kTouchContacts = 5;

struct TouchContact {
    uint8_t id;    // the finger, stable while it is down
    uint8_t state; // 1 down, 2 moved, 3 up
    uint16_t x, y; // panel pixels
};

struct TouchSample {
    size_t count;
    TouchContact contacts[kTouchContacts];
};

/* The touch controller's messages out of its datagrams: a 16-byte header
 * (ident B0 04 80 02, length), then 8-byte records `00 type flags seq` + 4
 * data bytes; a message runs from a start record (type bit 2) to an end
 * record (bit 1), in one datagram or several, its sequence numbers in a row.
 * The data of its records 4.. is the stream: a counter, the contact count,
 * then 6 bytes per contact (x, y big-endian, size, state << 4 | id). Pure,
 * no allocation. */
class TouchFrames {
public:
    TouchFrames();
    /* One datagram; returns how many samples it completed (at most `max`, in
     * `out`), or -1 when it is no touch datagram. */
    int feed(const uint8_t *datagram, size_t size, TouchSample *out, size_t max);

private:
    bool finish(TouchSample &out) const;
    uint8_t data_[32 * 4];
    size_t records_;
    uint8_t sequence_;
    bool open_;
};

/* One finger's gestures on the manager's screen, from the samples: a tap
 * where it went down and up, a drag in steps as it moves up or down, a swipe
 * to the right for BACK. The first finger down leads; others are ignored. */
struct TouchGesture {
    enum Kind { None, Tap, Step, Back };
    Kind kind;
    int x, y;  // Tap: where, panel pixels
    int steps; // Step: rows, positive when the finger moved up (the list goes on)
};

class TouchTracker {
public:
    /* `stepPx`: finger travel per row; `slopPx`: travel a tap forgives;
     * `backPx`: travel to the right that means BACK. */
    TouchTracker(int stepPx, int slopPx, int backPx);
    void configure(int stepPx, int slopPx, int backPx);
    /* At most one gesture per sample. */
    TouchGesture feed(const TouchSample &sample);
    void reset();

private:
    int stepPx_, slopPx_, backPx_;
    bool down_, dragging_, swiped_;
    uint8_t id_;
    int startX_, startY_, lastX_, lastY_, stepY_;
};

/* The UDP socket, bound only while owned. */
class TouchSocket {
public:
    TouchSocket();
    /* `port` 0: no touch screen to read (desktop builds without a test port). */
    void setPort(uint16_t port);
    /* Binds or closes; true when the state changed. */
    bool own(bool owned);
    bool owned() const {
        return fd_.valid();
    }
    int fd() const {
        return fd_.get();
    }
    /* The next datagram, or 0 when none waits. */
    size_t receive(uint8_t *out, size_t size);

private:
    core::UniqueFd fd_;
    uint16_t port_;
    bool failed_;
};

} // namespace gui
} // namespace nbt

#endif
