// SPDX-License-Identifier: MPL-2.0
/* Finding the car's gateway when the address in use does not answer
 * (docs/engine-diagnostics.md). Cars differ: the head unit's OBD link (dwc0)
 * reaches the gateway only while something is plugged into the OBD port; a
 * G-series head unit also sits on the in-car Ethernet with it; the gateway's
 * address changes between cars. So, for any car, the search:
 *  - logs what the head unit knows of each wired link (its address, routers
 *    and neighbours: what a laptop's ifconfig and arp would show);
 *  - asks every link who the gateway is (HSFZ vehicle identification);
 *  - tries on the diagnostic port those that answered, the address BMW's
 *    in-car addressing gives the gateway (host 0x10, its diagnostic address,
 *    of the link's network), the usual address, then the links' routers and
 *    neighbours;
 *  - takes the first that answers a tester present to the gateway's own
 *    address 0x10, with another tester address when ours is refused, and
 *    remembers it (state/manager/diag-gateway) so the next start tries it
 *    first. Failing that, a neighbour on the OBD link that takes the port. */
#include "nbt/diag/gateway_ident.hpp"
#include "nbt/diag/link_facts.hpp"
#include "nbt/log.h"
#include "nbt/process/diag_service.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

const int kTryMs = 400, kAnswerMs = 600, kIdentifyMs = 700;
// The gateway's own diagnostic address: it answers a tester present there,
// another control unit does not route it.
const uint8_t kGatewayEcu = 0x10;
// The usual gateway on the OBD link (docs/engine-diagnostics.md).
const char kUsualGateway[] = "169.254.21.3:6801";
const size_t kCandidatesMax = 40;

const char *text(uint32_t address, char out[16]) {
    struct in_addr value;
    value.s_addr = address;
    strncpy(out, inet_ntoa(value), 15);
    out[15] = 0;
    return out;
}

bool linkLocal(uint32_t address) {
    return (ntohl(address) & 0xffff0000u) == 0xa9fe0000u; // 169.254.0.0/16
}

struct Candidate {
    uint32_t address;
    uint16_t port;
    const char *why;
    const char *link;
    bool obd; // on the OBD link: may stand in when nothing proves a gateway
};

struct Candidates {
    Candidate items[kCandidatesMax];
    size_t count;
    const diag::LinkFacts *links;
    size_t linkCount;

    void add(uint32_t address, uint16_t port, const char *why, const char *link, bool obd) {
        if (!address || count >= kCandidatesMax) {
            return;
        }
        for (size_t i = 0; i < linkCount; ++i) {
            if (links[i].address == address) {
                return; // the head unit itself
            }
        }
        for (size_t i = 0; i < count; ++i) {
            if (items[i].address == address && items[i].port == port) {
                return;
            }
        }
        const Candidate c = {address, port, why, link, obd};
        items[count++] = c;
    }
};

} // namespace

// 2: the gateway (a tester present to 0x10 answered); 1: takes the port, not
// proven; 0: nobody there.
int DiagService::tryGateway(uint32_t address, uint16_t port, const char *why, bool verbose) {
    char name[16];
    if (!hsfz_.connect(address, port, kTryMs)) {
        if (verbose) {
            nbt_logf("host %s: diag: %s (%s) does not take the diagnostic port (errno %d)", app_,
                     text(address, name), why, hsfz_.connectError());
        }
        return 0;
    }
    const uint8_t present[] = {0x3E, 0x00};
    for (size_t t = 0; t < sizeof(diag::kTesterAddresses); ++t) {
        const uint8_t tester = diag::kTesterAddresses[t];
        if (t && !hsfz_.connect(address, port, kTryMs)) {
            break;
        }
        hsfz_.setTester(tester);
        uint8_t answer[16];
        size_t size = 0;
        const diag::HsfzTester::Result result = hsfz_.request(
            kGatewayEcu, present, sizeof(present), answer, sizeof(answer), &size, kAnswerMs);
        if (result == diag::HsfzTester::Ok) {
            nbt_logf("host %s: diag: %s (%s) takes the diagnostic port; tester present to 0x%02x "
                     "from tester 0x%02X: answered",
                     app_, text(address, name), why, static_cast<unsigned>(kGatewayEcu), tester);
            return 2;
        }
        const unsigned refusal = result == diag::HsfzTester::Refused ? hsfz_.refusal() : 0;
        nbt_logf("host %s: diag: %s (%s) takes the diagnostic port; tester present to 0x%02x "
                 "from tester 0x%02X: %s (HSFZ 0x%04x)",
                 app_, text(address, name), why, static_cast<unsigned>(kGatewayEcu), tester,
                 result == diag::HsfzTester::Refused ? "refused" : "no answer", refusal);
        hsfz_.close();
        if (refusal != 0x0040) { // only a refused tester address is worth another
            break;
        }
    }
    hsfz_.close();
    hsfz_.setTester(diag::kTesterAddress);
    return 1;
}

bool DiagService::findGateway() {
    char name[16], facts[240];
    const int error = hsfz_.connectError();
    diag::LinkName names[diag::kCarLinksMax];
    diag::LinkFacts links[diag::kCarLinksMax];
    const size_t count = diag::carLinks(names, diag::kCarLinksMax);
    for (size_t i = 0; i < count; ++i) {
        if (!diag::linkFacts(names[i].name, &links[i])) {
            memset(&links[i], 0, sizeof(links[i]));
        }
    }
    // What the head unit knows of each link, the OBD link first.
    const bool obdFirst = count && !strcmp(names[0].name, diag::kGatewayLink) && links[0].present;
    if (obdFirst) {
        diag::linkFactsText(links[0], facts, sizeof(facts));
        nbt_logf("host %s: diag: gateway %s:%u did not answer (errno %d); %s is %s", app_,
                 text(ip_, name), static_cast<unsigned>(port_), error, diag::kGatewayLink, facts);
    } else {
        nbt_logf("host %s: diag: gateway %s:%u did not answer (errno %d); %s not read", app_,
                 text(ip_, name), static_cast<unsigned>(port_), error, diag::kGatewayLink);
    }
    for (size_t i = obdFirst ? 1 : 0; i < count; ++i) {
        diag::linkFactsText(links[i], facts, sizeof(facts));
        nbt_logf("host %s: diag: %s is %s", app_, names[i].name, facts);
    }
    // Who says it is a gateway.
    int sendError[diag::kCarLinksMax];
    diag::Identified identified[kCandidatesMax];
    const size_t answered = count ? diag::identifyGateways(links, count, sendError, kIdentifyMs,
                                                           identified, kCandidatesMax)
                                  : 0;
    for (size_t i = 0; i < count; ++i) {
        if (sendError[i]) {
            nbt_logf("host %s: diag: %s: vehicle identification not sent (errno %d)", app_,
                     names[i].name, sendError[i]);
        }
    }
    for (size_t i = 0; i < answered; ++i) {
        nbt_logf("host %s: diag: %s answers vehicle identification on %s (diagnostic address "
                 "0x%02X)",
                 app_, text(identified[i].address, name), names[identified[i].link].name,
                 static_cast<unsigned>(identified[i].diagAddress));
    }
    if (count && !answered) {
        nbt_logf("host %s: diag: no answer to vehicle identification on %u links", app_,
                 static_cast<unsigned>(count));
    }
    // Where to look, the likeliest first.
    Candidates list;
    memset(&list, 0, sizeof(list));
    list.links = links;
    list.linkCount = count;
    for (int pass = 0; pass < 2; ++pass) { // the gateway's own answer first
        for (size_t i = 0; i < answered; ++i) {
            if ((identified[i].diagAddress == kGatewayEcu) == (pass == 0)) {
                const size_t l = identified[i].link;
                list.add(identified[i].address, port_, "identified", names[l].name,
                         !strcmp(names[l].name, diag::kGatewayLink));
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const uint32_t hosts = ntohl(~links[i].netmask);
        if (links[i].address && !linkLocal(links[i].address) && hosts > kGatewayEcu) {
            list.add((links[i].address & links[i].netmask) | htonl(kGatewayEcu), port_,
                     "in-car address", names[i].name, false);
        }
    }
    uint32_t usual = 0;
    uint16_t usualPort = 0;
    if (diag::parseGateway(kUsualGateway, &usual, &usualPort) && usual != ip_) {
        list.add(usual, usualPort, "usual address", diag::kGatewayLink, true);
    }
    for (size_t i = 0; i < count; ++i) {
        const bool obd = !strcmp(names[i].name, diag::kGatewayLink);
        for (size_t j = 0; j < links[i].count; ++j) {
            if (links[i].candidates[j] != ip_) {
                list.add(links[i].candidates[j], port_, "neighbour", names[i].name, obd);
            }
        }
    }
    // The first that proves a gateway; failing that, the first on the OBD
    // link that takes the port.
    const Candidate *open = NULL;
    for (size_t i = 0; i < list.count; ++i) {
        const Candidate &c = list.items[i];
        char why[48];
        snprintf(why, sizeof(why), "%s on %s", c.why, c.link);
        const int found = tryGateway(c.address, c.port, why, strcmp(c.why, "neighbour") != 0);
        if (found == 2) {
            ip_ = c.address;
            port_ = c.port;
            nbt_logf("host %s: diag: gateway found at %s (%s, tester 0x%02X)", app_,
                     text(c.address, name), why, hsfz_.tester());
            remember();
            return true;
        }
        if (found == 1 && c.obd && !open) {
            open = &c;
        }
    }
    if (open && hsfz_.connect(open->address, open->port, kTryMs)) {
        ip_ = open->address;
        port_ = open->port;
        nbt_logf("host %s: diag: using %s, the one address that takes the port", app_,
                 text(open->address, name));
        return true;
    }
    nbt_logf("host %s: diag: no gateway among %u addresses on %u links", app_,
             static_cast<unsigned>(list.count), static_cast<unsigned>(count));
    return false;
}

void DiagService::remember() {
    char line[48], name[16];
    const int n = snprintf(line, sizeof(line), "%s:%u %02X\n", text(ip_, name),
                           static_cast<unsigned>(port_), hsfz_.tester());
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line) ||
        nbt_fs_replace_text(fs_, NBT_FS_DIAG_GATEWAY_NEXT, NBT_FS_DIAG_GATEWAY, "manager", line,
                            static_cast<size_t>(n))) {
        nbt_logf("host %s: diag: cannot remember the gateway (errno %d)", app_, errno);
    }
}

bool DiagService::remembered(char *gateway, size_t size, uint8_t *tester) const {
    char line[64];
    const int fd = nbt_fs_open(fs_, NBT_FS_DIAG_GATEWAY, "manager", NULL, NBT_FS_READ);
    if (fd < 0) {
        return false;
    }
    const ssize_t n = read(fd, line, sizeof(line) - 1);
    close(fd);
    if (n <= 0) {
        return false;
    }
    line[n] = 0;
    // "address:port tester": a tester address the search tried, the usual one otherwise.
    char address[48];
    unsigned value = diag::kTesterAddress;
    uint32_t ip = 0;
    uint16_t port = 0;
    if (sscanf(line, "%47s %x", address, &value) < 1 || !diag::parseGateway(address, &ip, &port) ||
        strlen(address) >= size) {
        return false;
    }
    *tester = diag::kTesterAddress;
    for (size_t t = 0; t < sizeof(diag::kTesterAddresses); ++t) {
        if (diag::kTesterAddresses[t] == value) {
            *tester = static_cast<uint8_t>(value);
        }
    }
    memcpy(gateway, address, strlen(address) + 1);
    return true;
}

} // namespace process
} // namespace nbt
