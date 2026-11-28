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

#ifdef NBT_TESTING
uint16_t identPort() {
    const char *port = getenv("NBT_TEST_DIAG_IDENT_PORT");
    return port && *port ? static_cast<uint16_t>(atoi(port)) : kIdentPort;
}
#else
uint16_t identPort() {
    return kIdentPort;
}
#endif

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

} // namespace

size_t identifyGateways(const LinkFacts *links, size_t count, int *sendError, int timeoutMs,
                        Identified *out, size_t max) {
    static const uint8_t kRequest[] = {0, 0, 0, 0, 0, 0x11};
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
        sockaddr_in local, broadcast;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = links[i].address;
        broadcast = local;
        broadcast.sin_port = htons(identPort());
        broadcast.sin_addr.s_addr = links[i].address | ~links[i].netmask;
        if (fd < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) ||
            setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) ||
            bind(fd, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) ||
            sendto(fd, kRequest, sizeof(kRequest), 0,
                   reinterpret_cast<const sockaddr *>(&broadcast),
                   sizeof(broadcast)) != static_cast<ssize_t>(sizeof(kRequest))) {
            sendError[i] = errno ? errno : EIO;
            if (fd >= 0) {
                close(fd);
            }
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
            // An identification answer: a frame with control 0x0011, from
            // someone else than this link's own address.
            if (size < 6 || answer[4] != 0 || answer[5] != 0x11 ||
                from.sin_addr.s_addr == links[watched[k]].address) {
                continue;
            }
            bool seen = false;
            for (size_t j = 0; j < found; ++j) {
                seen = seen || out[j].address == from.sin_addr.s_addr;
            }
            if (!seen && found < max) {
                out[found].address = from.sin_addr.s_addr;
                out[found].diagAddress = namedAddress(answer + 6, static_cast<size_t>(size) - 6);
                out[found].link = watched[k];
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
