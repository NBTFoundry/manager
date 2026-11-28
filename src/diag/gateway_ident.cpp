// SPDX-License-Identifier: MPL-2.0
#include "nbt/diag/gateway_ident.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace diag {
namespace {

uint16_t testPort(const char *name, uint16_t port) {
#ifdef NBT_TESTING
    const char *value = getenv(name);
    return value && *value ? static_cast<uint16_t>(atoi(value)) : port;
#else
    (void)name;
    return port;
#endif
}

uint16_t identPort() {
    return testPort("NBT_TEST_DIAG_IDENT_PORT", kIdentPort);
}

bool sendTo(int fd, uint32_t address, uint16_t port, const uint8_t *data, size_t size) {
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = address;
    return sendto(fd, data, size, 0, reinterpret_cast<const sockaddr *>(&to), sizeof(to)) ==
           static_cast<ssize_t>(size);
}

// Who an answer names: HSFZ (control 0x0011) "DIAGADR10", DoIP (payload type
// 0x0004) the logical address after the VIN; false when it is neither.
bool answerOf(const uint8_t *answer, size_t size, bool *doip, uint16_t *named);

uint64_t nowMs() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000u + static_cast<uint64_t>(t.tv_nsec) / 1000000u;
}

// "DIAGADR10..." in an identification answer: the diagnostic address it names.
uint8_t namedAddress(const uint8_t *data, size_t size) {
    static const char kTag[] = "DIAGADR";
    const size_t tag = sizeof(kTag) - 1;
    for (size_t i = 0; i + tag + 2 <= size; ++i) {
        if (!memcmp(data + i, kTag, tag)) {
            char hex[3] = {static_cast<char>(data[i + tag]), static_cast<char>(data[i + tag + 1]),
                           0};
            char *end = NULL;
            const unsigned long value = strtoul(hex, &end, 16);
            return end == hex + 2 ? static_cast<uint8_t>(value) : 0xFF;
        }
    }
    return 0xFF;
}

bool answerOf(const uint8_t *answer, size_t size, bool *doip, uint16_t *named) {
    if (size >= 8 + 19 && answer[0] == 0x02 && answer[1] == 0xFD && answer[2] == 0x00 &&
        answer[3] == 0x04) {
        *doip = true;
        *named = static_cast<uint16_t>(answer[8 + 17] << 8 | answer[8 + 18]);
        return true;
    }
    if (size >= 6 && answer[4] == 0 && answer[5] == 0x11) {
        *doip = false;
        const uint8_t address = namedAddress(answer + 6, size - 6);
        *named = address == 0xFF ? 0xFFFF : address;
        return true;
    }
    return false;
}

} // namespace

uint16_t doipPort() {
    return testPort("NBT_TEST_DIAG_DOIP_PORT", kDoipPort);
}

size_t identifyGateways(const LinkFacts *links, size_t count, int *sendError, int timeoutMs,
                        Identified *out, size_t max) {
    static const uint8_t kRequest[] = {0, 0, 0, 0, 0, 0x11};
    static const uint8_t kDoipRequest[] = {0x02, 0xFD, 0x00, 0x01, 0, 0, 0, 0};
    int fds[kCarLinksMax];
    size_t found = 0;
    for (size_t i = 0; i < count && i < kCarLinksMax; ++i) {
        fds[i] = -1;
        sendError[i] = 0;
        if (!links[i].present || !links[i].address) {
            sendError[i] = EADDRNOTAVAIL;
            continue;
        }
        const int fd = socket(AF_INET, SOCK_DGRAM, 0);
        const int on = 1;
        sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = links[i].address;
        const uint32_t broadcast = links[i].address | ~links[i].netmask;
        if (fd < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) ||
            setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) ||
            bind(fd, reinterpret_cast<const sockaddr *>(&local), sizeof(local))) {
            sendError[i] = errno ? errno : EIO;
            if (fd >= 0) {
                close(fd);
            }
            continue;
        }
        errno = 0;
        const bool hsfz = sendTo(fd, broadcast, identPort(), kRequest, sizeof(kRequest));
        sendError[i] = hsfz ? 0 : (errno ? errno : EIO);
        const bool doip = sendTo(fd, broadcast, doipPort(), kDoipRequest, sizeof(kDoipRequest));
        if (!hsfz && !doip) {
            close(fd);
            continue;
        }
        fds[i] = fd;
    }
    const uint64_t deadline = nowMs() + static_cast<uint64_t>(timeoutMs > 0 ? timeoutMs : 0);
    for (;;) {
        struct pollfd ready[kCarLinksMax];
        size_t watched[kCarLinksMax], n = 0;
        for (size_t i = 0; i < count && i < kCarLinksMax; ++i) {
            if (fds[i] >= 0) {
                ready[n].fd = fds[i];
                ready[n].events = POLLIN;
                ready[n].revents = 0;
                watched[n++] = i;
            }
        }
        const uint64_t now = nowMs();
        if (!n || now >= deadline || poll(ready, n, static_cast<int>(deadline - now)) <= 0) {
            break;
        }
        for (size_t k = 0; k < n; ++k) {
            if (!(ready[k].revents & POLLIN)) {
                continue;
            }
            uint8_t answer[512];
            sockaddr_in from;
            socklen_t fromSize = sizeof(from);
            const ssize_t size = recvfrom(ready[k].fd, answer, sizeof(answer), 0,
                                          reinterpret_cast<sockaddr *>(&from), &fromSize);
            // An identification answer from someone else than this link's address.
            bool doip = false;
            uint16_t named = 0xFFFF;
            if (size <= 0 || from.sin_addr.s_addr == links[watched[k]].address ||
                !answerOf(answer, static_cast<size_t>(size), &doip, &named)) {
                continue;
            }
            bool seen = false;
            for (size_t j = 0; j < found; ++j) {
                seen = seen || (out[j].address == from.sin_addr.s_addr && out[j].doip == doip);
            }
            if (!seen && found < max) {
                out[found].address = from.sin_addr.s_addr;
                out[found].diagAddress = named;
                out[found].link = watched[k];
                out[found].doip = doip;
                ++found;
            }
        }
    }
    for (size_t i = 0; i < count && i < kCarLinksMax; ++i) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }
    return found;
}

} // namespace diag
} // namespace nbt
