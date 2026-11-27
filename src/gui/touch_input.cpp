// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/touch_input.hpp"

#include "nbt/log.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nbt {
namespace gui {
namespace {

const size_t kHeader = 16, kRecord = 8, kRecordsMax = 32;
const size_t kStreamRecord = 4; // the stream starts in the message's fifth record
const uint8_t kIdent[4] = {0xB0, 0x04, 0x80, 0x02};

int distance(int a, int b) {
    return a > b ? a - b : b - a;
}

} // namespace

TouchFrames::TouchFrames() : data_(), records_(0), sequence_(0), open_(false) {
}

bool TouchFrames::finish(TouchSample &out) const {
    const size_t base = kStreamRecord * 4;
    memset(&out, 0, sizeof(out));
    if (records_ * 4 < base + 2) {
        return false;
    }
    const uint8_t *stream = data_ + base;
    const size_t available = records_ * 4 - base;
    size_t declared = stream[1];
    declared = declared > kTouchContacts ? kTouchContacts : declared;
    for (size_t c = 0; c < declared; ++c) {
        const size_t at = 2 + 6 * c;
        if (at + 6 > available) {
            break;
        }
        TouchContact contact;
        contact.x = static_cast<uint16_t>(stream[at] << 8 | stream[at + 1]);
        contact.y = static_cast<uint16_t>(stream[at + 2] << 8 | stream[at + 3]);
        contact.state = static_cast<uint8_t>(stream[at + 5] >> 4);
        contact.id = static_cast<uint8_t>(stream[at + 5] & 0x0F);
        if (contact.state < 1 || contact.state > 3 || contact.id >= kTouchContacts) {
            continue; // nothing this reader knows
        }
        out.contacts[out.count++] = contact;
    }
    return true;
}

int TouchFrames::feed(const uint8_t *datagram, size_t size, TouchSample *out, size_t max) {
    if (!datagram || size < kHeader + kRecord || memcmp(datagram, kIdent, sizeof(kIdent))) {
        return -1;
    }
    const size_t length =
        static_cast<size_t>(datagram[4]) << 24 | datagram[5] << 16 | datagram[6] << 8 | datagram[7];
    if (length + 8 < size && length + 8 >= kHeader + kRecord) {
        size = length + 8; // bytes after the message are not its records
    }
    int done = 0;
    for (size_t r = 0; r < (size - kHeader) / kRecord; ++r) {
        const uint8_t *record = datagram + kHeader + kRecord * r;
        const uint8_t type = record[1];
        if (record[0] != 0 || (type & 0xC0) != 0xC0) {
            open_ = false;
            return done ? done : -1;
        }
        if (type & 0x04) { // a message starts; one that never ended is dropped
            open_ = true;
            records_ = 0;
        } else if (!open_) {
            continue; // the rest of a message whose start was missed
        } else if (record[3] != static_cast<uint8_t>(sequence_ + 1)) {
            open_ = false; // a datagram was lost
            continue;
        }
        sequence_ = record[3];
        if (records_ >= kRecordsMax) {
            open_ = false;
            continue;
        }
        memcpy(data_ + records_ * 4, record + 4, 4);
        ++records_;
        if (type & 0x02) {
            open_ = false;
            if (out && static_cast<size_t>(done) < max && finish(out[done])) {
                ++done;
            }
        }
    }
    return done;
}

TouchTracker::TouchTracker(int stepPx, int slopPx, int backPx)
    : stepPx_(1), slopPx_(1), backPx_(1), down_(false), dragging_(false), swiped_(false), id_(0),
      startX_(0), startY_(0), lastX_(0), lastY_(0), stepY_(0) {
    configure(stepPx, slopPx, backPx);
}

void TouchTracker::configure(int stepPx, int slopPx, int backPx) {
    stepPx_ = stepPx > 0 ? stepPx : 1;
    slopPx_ = slopPx > 0 ? slopPx : 1;
    backPx_ = backPx > 0 ? backPx : 1;
}

void TouchTracker::reset() {
    down_ = dragging_ = swiped_ = false;
}

TouchGesture TouchTracker::feed(const TouchSample &sample) {
    TouchGesture gesture = {TouchGesture::None, 0, 0, 0};
    const TouchContact *lead = NULL;
    for (size_t i = 0; i < sample.count && !lead; ++i) {
        const TouchContact &c = sample.contacts[i];
        if (down_ ? c.id == id_ : c.state != 3) {
            lead = &c;
        }
    }
    if (!down_) {
        if (lead) { // a finger went down (or its first sample was missed)
            down_ = true;
            dragging_ = swiped_ = false;
            id_ = lead->id;
            startX_ = lastX_ = lead->x;
            startY_ = lastY_ = stepY_ = lead->y;
        }
        return gesture;
    }
    if (!lead || lead->state == 3) { // up, or gone from the samples
        if (lead) {
            lastX_ = lead->x;
            lastY_ = lead->y;
        }
        down_ = false;
        if (!dragging_ && !swiped_ && distance(lastX_, startX_) <= slopPx_ &&
            distance(lastY_, startY_) <= slopPx_) {
            gesture.kind = TouchGesture::Tap;
            gesture.x = startX_;
            gesture.y = startY_;
        }
        return gesture;
    }
    lastX_ = lead->x;
    lastY_ = lead->y;
    const int right = lastX_ - startX_, vertical = distance(lastY_, startY_);
    if (swiped_) {
        return gesture;
    }
    if (!dragging_) {
        if (right >= backPx_ && vertical * 2 < right) {
            swiped_ = true;
            gesture.kind = TouchGesture::Back;
            return gesture;
        }
        if (vertical > slopPx_ && vertical >= distance(lastX_, startX_)) {
            dragging_ = true;
        } else if (distance(lastX_, startX_) > slopPx_ && right < backPx_) {
            return gesture; // sideways: no tap, maybe a swipe yet
        }
    }
    if (dragging_) {
        const int steps = (stepY_ - lastY_) / stepPx_;
        if (steps) {
            stepY_ -= steps * stepPx_;
            gesture.kind = TouchGesture::Step;
            gesture.steps = steps;
        }
    }
    return gesture;
}

TouchSocket::TouchSocket() : fd_(), port_(0), failed_(false) {
}

void TouchSocket::setPort(uint16_t port) {
    port_ = port;
}

bool TouchSocket::own(bool owned) {
    if (!owned) {
        failed_ = false;
        if (!fd_.valid()) {
            return false;
        }
        fd_.reset(); // iDrive's socket takes the datagrams again
        return true;
    }
    if (fd_.valid() || failed_ || !port_) {
        return false;
    }
    core::UniqueFd fd(socket(AF_INET, SOCK_DGRAM, 0));
    const int on = 1;
    struct sockaddr_in where;
    memset(&where, 0, sizeof(where));
    where.sin_family = AF_INET;
    where.sin_port = htons(port_);
    where.sin_addr.s_addr = htonl(INADDR_ANY);
    bool ready = fd.valid() && !setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
    ready = ready && !setsockopt(fd.get(), SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
    ready = ready && !bind(fd.get(), reinterpret_cast<struct sockaddr *>(&where), sizeof(where)) &&
            !fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL) | O_NONBLOCK) &&
            !fcntl(fd.get(), F_SETFD, FD_CLOEXEC);
    if (!ready) {
        failed_ = true; // once per showing: no touch, the controller as before
        nbt_logf("touch: UDP %u not taken (errno %d)", static_cast<unsigned>(port_), errno);
        return false;
    }
    fd_ = std::move(fd);
    return true;
}

size_t TouchSocket::receive(uint8_t *out, size_t size) {
    if (!fd_.valid()) {
        return 0;
    }
    ssize_t n;
    do {
        n = recv(fd_.get(), out, size, MSG_DONTWAIT);
    } while (n < 0 && errno == EINTR);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

} // namespace gui
} // namespace nbt
