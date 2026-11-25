// SPDX-License-Identifier: MPL-2.0
#include "nbt/diag/hsfz_tester.hpp"

#include "nbt/util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nbt {
namespace diag {
namespace {

enum {
    kDiag = 0x0001,
    kAck = 0x0002,
    kAliveCheck = 0x0012,
    kAliveReply = 0x0013,
    kHeader = 6,
};

void putHeader(uint8_t *out, uint32_t length, uint16_t control) {
    out[0] = static_cast<uint8_t>(length >> 24);
    out[1] = static_cast<uint8_t>(length >> 16);
    out[2] = static_cast<uint8_t>(length >> 8);
    out[3] = static_cast<uint8_t>(length);
    out[4] = static_cast<uint8_t>(control >> 8);
    out[5] = static_cast<uint8_t>(control);
}

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

} // namespace

bool parseGateway(const char *text, uint32_t *ip, uint16_t *port) {
    unsigned a, b, c, d, p = kHsfzPort;
    char tail = 0;
    if (!text || !ip || !port) {
        return false;
    }
    const int n = sscanf(text, "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &p, &tail);
    if ((n != 4 && n != 5) || a > 255 || b > 255 || c > 255 || d > 255 || !p || p > 65535) {
        return false;
    }
    *ip = htonl(a << 24 | b << 16 | c << 8 | d);
    *port = static_cast<uint16_t>(p);
    return true;
}

HsfzTester::HsfzTester() : fd_(-1), buffer_(), length_(0), consumed_(0) {
}

HsfzTester::~HsfzTester() {
    close();
}

bool HsfzTester::connect(uint32_t ip, uint16_t port, int timeoutMs) {
    close();
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    const int flags = fcntl(fd, F_GETFL);
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = ip;
    int error = 0;
    socklen_t size = sizeof(error);
    struct pollfd p = {fd, POLLOUT, 0};
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) ||
        (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) &&
         errno != EINPROGRESS) ||
        poll(&p, 1, timeoutMs) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) ||
        error) {
        ::close(fd);
        return false;
    }
    const int on = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    fd_ = fd;
    length_ = consumed_ = 0;
    return true;
}

void HsfzTester::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    length_ = consumed_ = 0;
}

bool HsfzTester::sendAll(const uint8_t *data, size_t size) {
    const uint64_t deadline = after(1000);
    while (size) {
        const ssize_t n = send(fd_, data, size, 0);
        if (n > 0) {
            data += n;
            size -= static_cast<size_t>(n);
            continue;
        }
        struct pollfd p = {fd_, POLLOUT, 0};
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) &&
            remaining(deadline) > 0 && poll(&p, 1, remaining(deadline)) >= 0) {
            continue;
        }
        return false;
    }
    return true;
}

bool HsfzTester::fill(int timeoutMs) {
    struct pollfd p = {fd_, POLLIN, 0};
    const int ready = poll(&p, 1, timeoutMs);
    if (ready <= 0) {
        return ready == 0 || errno == EINTR;
    }
    if (consumed_) {
        memmove(buffer_, buffer_ + consumed_, length_ - consumed_);
        length_ -= consumed_;
        consumed_ = 0;
    }
    if (length_ == sizeof(buffer_)) {
        return false; // a frame larger than any we take
    }
    const ssize_t n = recv(fd_, buffer_ + length_, sizeof(buffer_) - length_, 0);
    if (n > 0) {
        length_ += static_cast<size_t>(n);
        return true;
    }
    return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
}

bool HsfzTester::nextFrame(uint16_t *control, const uint8_t **payload, size_t *size) {
    const size_t available = length_ - consumed_;
    if (available < kHeader) {
        return false;
    }
    const uint8_t *p = buffer_ + consumed_;
    const uint32_t length = static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
                            static_cast<uint32_t>(p[2]) << 8 | p[3];
    if (length > kHsfzFrameMax - kHeader || available < kHeader + length) {
        return false;
    }
    *control = static_cast<uint16_t>(p[4] << 8 | p[5]);
    *payload = p + kHeader;
    *size = length;
    consumed_ += kHeader + length;
    return true;
}

HsfzTester::Result HsfzTester::request(uint8_t ecu, const uint8_t *uds, size_t size,
                                       uint8_t *answer, size_t capacity, size_t *answerSize,
                                       int timeoutMs) {
    *answerSize = 0;
    uint8_t frame[kHsfzFrameMax];
    if (fd_ < 0 || !uds || !size || size + 2 > kHsfzFrameMax - kHeader) {
        return Broken;
    }
    putHeader(frame, static_cast<uint32_t>(size + 2), kDiag);
    frame[6] = kTesterAddress;
    frame[7] = ecu;
    memcpy(frame + 8, uds, size);
    if (!sendAll(frame, kHeader + 2 + size)) {
        return Broken;
    }
    uint64_t deadline = after(timeoutMs);
    int pending = 0;
    for (;;) {
        uint16_t control = 0;
        const uint8_t *payload = NULL;
        size_t length = 0;
        while (nextFrame(&control, &payload, &length)) {
            if (control == kAliveCheck) {
                uint8_t reply[kHeader + 2];
                putHeader(reply, 2, kAliveReply);
                reply[6] = 0x00;
                reply[7] = kTesterAddress;
                if (!sendAll(reply, sizeof(reply))) {
                    return Broken;
                }
            } else if (control >= 0x0040 && control <= 0x0045) {
                return Refused; // bad tester, control, format, target, size, not ready
            } else if (control == 0x00FF) {
                return Refused;
            } else if (control == kDiag && length >= 3 && payload[0] == ecu &&
                       payload[1] == kTesterAddress) {
                const uint8_t *reply = payload + 2;
                const size_t n = length - 2;
                if (n >= 3 && reply[0] == 0x7F && reply[1] == uds[0] && reply[2] == 0x78 &&
                    pending < 5) {
                    ++pending;
                    deadline = after(timeoutMs);
                    continue;
                }
                // Ours: the positive answer, or a negative one for our service;
                // anything else answers an earlier request that timed out.
                if (reply[0] == (uds[0] | 0x40) ||
                    (n >= 2 && reply[0] == 0x7F && reply[1] == uds[0])) {
                    const size_t copy = n < capacity ? n : capacity;
                    memcpy(answer, reply, copy);
                    *answerSize = copy;
                    return Ok;
                }
            }
            // Acknowledgements (0x0002) and anything else: skipped.
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
