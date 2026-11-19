#include "nbt/worker/phone_network.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/gui/phone_wake.hpp"
#include "nbt/util.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#ifdef __QNX__
#include <net/if_dl.h>
#else
#include <netpacket/packet.h>
#endif
namespace nbt {
namespace worker {
void phoneBssid(const char *iface, char output[18]) {
    output[0] = 0;
    struct ifaddrs *addresses = NULL;
    if (getifaddrs(&addresses)) {
        return;
    }
    for (struct ifaddrs *p = addresses; p; p = p->ifa_next) {
        if (!p->ifa_addr || strcmp(p->ifa_name, iface)) {
            continue;
        }
        const unsigned char *mac = NULL;
#ifdef __QNX__
        if (p->ifa_addr->sa_family == AF_LINK) {
            const struct sockaddr_dl *link =
                reinterpret_cast<const struct sockaddr_dl *>(p->ifa_addr);
            if (link->sdl_alen == 6) {
                mac = reinterpret_cast<const unsigned char *>(LLADDR(link));
            }
        }
#else
        if (p->ifa_addr->sa_family == AF_PACKET) {
            const struct sockaddr_ll *link =
                reinterpret_cast<const struct sockaddr_ll *>(p->ifa_addr);
            if (link->sll_halen == 6) {
                mac = link->sll_addr;
            }
        }
#endif
        if (mac) {
            snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
                     mac[4], mac[5]);
        }
    }
    freeifaddrs(addresses);
#ifdef NBT_TESTING
    if (!strcmp(iface, "lo")) {
        strcpy(output, "02:11:22:33:44:55");
    }
#endif
}
int phoneWake(storage::PhoneStore &phones, const char *path, uint64_t &lastWake, uint64_t now,
              core::Span<char> out) {
    if (!phones.openEnabled()) {
        snprintf(out.data(), out.size(), "WAKE\toff\n");
        return 200;
    }
    gui::PhoneWake wake;
    if (lastWake && now >= lastWake && now - lastWake < 30000) {
        if (!gui::phoneWakeRead(path, wake) || now < wake.when || now - wake.when > 30000) {
            return 429;
        }
        snprintf(out.data(), out.size(), "WAKE\t%s\n", gui::phoneWakeName(wake.state));
        return 200;
    }
    if (nbt_fs_random_tx(wake.id)) {
        return 503;
    }
    wake.when = now;
    wake.state = gui::WakeOpening;
    if (!gui::phoneWakeWrite(path, wake)) {
        return 503;
    }
    lastWake = now;
    snprintf(out.data(), out.size(), "WAKE\topening\n");
    return 202;
}
bool interfaceReady(const char *iface, char ipOut[40]) {
    if (ipOut) {
        ipOut[0] = 0;
    }
    if (!iface || strlen(iface) >= IFNAMSIZ) {
        return false;
    }
    core::UniqueFd fd(socket(AF_INET, SOCK_DGRAM, 0));
    if (!fd.valid()) {
        return false;
    }
    struct ifreq req;
    memset(&req, 0, sizeof(req));
    memcpy(req.ifr_name, iface, strlen(iface) + 1);
    if (ioctl(fd.get(), SIOCGIFFLAGS, &req) != 0 || (req.ifr_flags & IFF_UP) == 0) {
        return false;
    }
    memset(&req, 0, sizeof(req));
    memcpy(req.ifr_name, iface, strlen(iface) + 1);
    if (ioctl(fd.get(), SIOCGIFADDR, &req) != 0 || req.ifr_addr.sa_family != AF_INET) {
        return false;
    }
    const struct sockaddr_in *addr = reinterpret_cast<const struct sockaddr_in *>(&req.ifr_addr);
    char ip[INET_ADDRSTRLEN] = {0};
    if (addr->sin_addr.s_addr == htonl(INADDR_ANY) ||
        !inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip))) {
        return false;
    }
    if (ipOut) {
        memcpy(ipOut, ip, strlen(ip) + 1);
    }
    return true;
}
} // namespace worker
} // namespace nbt
