// SPDX-License-Identifier: MPL-2.0
#include "nbt/diag/link_facts.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifdef __QNXNTO__
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <sys/sysctl.h>
#endif

namespace nbt {
namespace diag {
namespace {

#if defined(NBT_TESTING) || defined(__QNXNTO__)
void add(LinkFacts *out, uint32_t address, bool loopback = false) {
    const uint32_t host = ntohl(address);
    if (!address || address == out->address || host == 0xffffffffu ||
        ((host >> 24) == 127 && !loopback) || (host >> 28) == 14 || out->count >= kLinkCandidates) {
        return; // nothing, itself, broadcast, loopback, multicast
    }
    for (size_t i = 0; i < out->count; ++i) {
        if (out->candidates[i] == address) {
            return;
        }
    }
    out->candidates[out->count++] = address;
}
#endif

#ifdef __QNXNTO__
size_t roundUp(size_t length) {
    return length ? (length + sizeof(long) - 1) & ~(sizeof(long) - 1) : sizeof(long);
}

/* The routing table's entries for interface `index`: the address at position
 * `which` of each (0 the destination: an ARP neighbour; 1 the router). */
void scan(int operation, int flags, unsigned index, int which, LinkFacts *out) {
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, operation, flags};
    size_t size = 0;
    if (sysctl(mib, 6, NULL, &size, NULL, 0) || !size || size > 256u * 1024u) {
        return;
    }
    char *table = static_cast<char *>(malloc(size));
    if (!table) {
        return;
    }
    if (!sysctl(mib, 6, table, &size, NULL, 0)) {
        for (size_t at = 0; at + sizeof(struct rt_msghdr) <= size;) {
            struct rt_msghdr message;
            memcpy(&message, table + at, sizeof(message));
            if (message.rtm_msglen < sizeof(message) || at + message.rtm_msglen > size) {
                break;
            }
            size_t offset = at + sizeof(message);
            const size_t end = at + message.rtm_msglen;
            for (int bit = 0; bit < 8 && message.rtm_index == index; ++bit) {
                if (!(message.rtm_addrs & (1 << bit))) {
                    continue;
                }
                struct sockaddr_in address;
                if (offset + 2 > end) {
                    break;
                }
                const size_t length = static_cast<unsigned char>(table[offset]);
                if (bit == which && length >= sizeof(address) && offset + sizeof(address) <= end) {
                    memcpy(&address, table + offset, sizeof(address));
                    if (address.sin_family == AF_INET) {
                        add(out, address.sin_addr.s_addr);
                    }
                }
                offset += roundUp(length);
            }
            at += message.rtm_msglen;
        }
    }
    free(table);
}
#endif

#ifdef NBT_TESTING
/* What a test's links look like: NBT_TEST_DIAG_LINK is dwc0's
 * "address,neighbour,neighbour"; NBT_TEST_DIAG_LINKS the others',
 * "vlan73=address,neighbour;vlan77=address". */
bool testLink(const char *interface, char *spec, size_t size) {
    if (!strcmp(interface, kGatewayLink)) {
        const char *test = getenv("NBT_TEST_DIAG_LINK");
        snprintf(spec, size, "%s", test ? test : "");
        return spec[0] != 0;
    }
    const char *links = getenv("NBT_TEST_DIAG_LINKS");
    const size_t n = strlen(interface);
    for (const char *p = links; p && *p;) {
        const char *end = strchr(p, ';');
        const size_t length = end ? static_cast<size_t>(end - p) : strlen(p);
        if (length > n && !strncmp(p, interface, n) && p[n] == '=') {
            snprintf(spec, size, "%.*s", static_cast<int>(length - n - 1), p + n + 1);
            return spec[0] != 0;
        }
        p = end ? end + 1 : NULL;
    }
    return false;
}
#endif

#if defined(NBT_TESTING) || defined(__QNXNTO__)
// A link to the car, not to a phone, the other processor or the head unit itself.
bool wiredCarLink(const char *name) {
    static const char *const kOthers[] = {"lo", "uap", "wfd", "en", "veom", "ppp", "bt", "wl"};
    for (size_t i = 0; i < sizeof(kOthers) / sizeof(kOthers[0]); ++i) {
        if (!strncmp(name, kOthers[i], strlen(kOthers[i]))) {
            return false;
        }
    }
    return name[0] != 0;
}

void addLink(LinkName *out, size_t max, size_t *count, const char *name) {
    if (*count >= max || strlen(name) >= sizeof(out[0].name) || !wiredCarLink(name)) {
        return;
    }
    for (size_t i = 0; i < *count; ++i) {
        if (!strcmp(out[i].name, name)) {
            return;
        }
    }
    memcpy(out[*count].name, name, strlen(name) + 1);
    ++*count;
}
#endif

} // namespace

size_t carLinks(LinkName *out, size_t max) {
    size_t count = 0;
    if (!out || !max) {
        return 0;
    }
#ifdef NBT_TESTING
    char spec[160];
    const char *links = getenv("NBT_TEST_DIAG_LINKS");
    if (testLink(kGatewayLink, spec, sizeof(spec)) || (links && *links)) {
        if (testLink(kGatewayLink, spec, sizeof(spec))) {
            addLink(out, max, &count, kGatewayLink);
        }
        for (const char *p = links; p && *p;) {
            const char *equals = strchr(p, '='), *end = strchr(p, ';');
            if (equals && (!end || equals < end)) {
                char name[16];
                snprintf(name, sizeof(name), "%.*s", static_cast<int>(equals - p), p);
                addLink(out, max, &count, name);
            }
            p = end ? end + 1 : NULL;
        }
        return count;
    }
#endif
#ifdef __QNXNTO__
    struct ifaddrs *list = NULL;
    if (!getifaddrs(&list)) {
        for (int pass = 0; pass < 2; ++pass) { // dwc0 first
            for (struct ifaddrs *i = list; i; i = i->ifa_next) {
                if (i->ifa_name && i->ifa_addr && i->ifa_addr->sa_family == AF_INET &&
                    (pass == 0) == !strcmp(i->ifa_name, kGatewayLink)) {
                    addLink(out, max, &count, i->ifa_name);
                }
            }
        }
        freeifaddrs(list);
    }
#endif
    return count;
}

bool linkFacts(const char *interface, LinkFacts *out) {
    if (!interface || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
#ifdef NBT_TESTING
    char test[160];
    if (testLink(interface, test, sizeof(test))) {
        char text[160];
        snprintf(text, sizeof(text), "%s", test);
        char *save = NULL;
        for (char *part = strtok_r(text, ",", &save); part; part = strtok_r(NULL, ",", &save)) {
            struct in_addr address;
            if (!inet_aton(part, &address)) {
                continue;
            }
            if (!out->present) {
                out->present = out->up = true;
                out->address = address.s_addr;
                out->netmask = htonl(0xffff0000u);
            } else {
                add(out, address.s_addr, true); // a test's gateways are on the loopback
            }
        }
        return out->present;
    }
#endif
#ifdef __QNXNTO__
    struct ifaddrs *list = NULL;
    if (!getifaddrs(&list)) {
        for (struct ifaddrs *i = list; i; i = i->ifa_next) {
            if (!i->ifa_name || strcmp(i->ifa_name, interface)) {
                continue;
            }
            out->present = true;
            out->up = (i->ifa_flags & IFF_UP) && (i->ifa_flags & IFF_RUNNING);
            if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !out->address) {
                struct sockaddr_in address;
                memcpy(&address, i->ifa_addr, sizeof(address));
                out->address = address.sin_addr.s_addr;
                if (i->ifa_netmask) {
                    memcpy(&address, i->ifa_netmask, sizeof(address));
                    out->netmask = address.sin_addr.s_addr;
                }
            }
        }
        freeifaddrs(list);
    }
    const unsigned index = out->present ? if_nametoindex(interface) : 0;
    if (index) {
        scan(NET_RT_FLAGS, RTF_GATEWAY, index, 1, out); // a router first
        scan(NET_RT_FLAGS, RTF_LLINFO, index, 0, out);  // then who it has talked to
    }
    return out->present;
#else
    return false;
#endif
}

void linkFactsText(const LinkFacts &facts, char *out, size_t size) {
    if (!out || !size) {
        return;
    }
    if (!facts.present) {
        snprintf(out, size, "no such interface");
        return;
    }
    char address[16], netmask[16];
    struct in_addr value;
    value.s_addr = facts.address;
    snprintf(address, sizeof(address), "%s", facts.address ? inet_ntoa(value) : "no address");
    value.s_addr = facts.netmask;
    snprintf(netmask, sizeof(netmask), "%s", inet_ntoa(value));
    int used = snprintf(out, size, "%s/%s %s, %s", address, netmask, facts.up ? "up" : "down",
                        facts.count ? "neighbours" : "no neighbour known");
    for (size_t i = 0; i < facts.count && used > 0 && static_cast<size_t>(used) < size; ++i) {
        value.s_addr = facts.candidates[i];
        used += snprintf(out + used, size - static_cast<size_t>(used), " %s", inet_ntoa(value));
    }
}

} // namespace diag
} // namespace nbt
