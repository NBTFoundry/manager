// SPDX-License-Identifier: MPL-2.0
/* One address the gateway search tries (diag_gateway.cpp), and the gateway
 * found remembered between starts (docs/engine-diagnostics.md). */
#include "nbt/diag/hsfz_tester.hpp"
#include "nbt/log.h"
#include "nbt/process/diag_service.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

const int kTryMs = 400, kAnswerMs = 600, kRouteMs = 2500;
// The gateway's own diagnostic address: it answers a tester present there,
// another control unit does not route it. A host that does not answer there
// but for the engine computer forwards to it, which is all diagnostics need.
const uint8_t kGatewayEcu = 0x10, kEngineEcu = 0x12;

const char *text(uint32_t address, char out[16]) {
    struct in_addr value;
    value.s_addr = address;
    strncpy(out, inet_ntoa(value), 15);
    out[15] = 0;
    return out;
}

} // namespace

void DiagService::testerText(char *out, size_t size) const {
    if (hsfz_.protocol() == diag::HsfzTester::Doip) {
        snprintf(out, size, "DoIP tester 0x%04X", static_cast<unsigned>(hsfz_.doipTester()));
    } else {
        snprintf(out, size, "tester 0x%02X", static_cast<unsigned>(hsfz_.tester()));
    }
}

// On the connection open: whether the gateway, or a host forwarding to the
// engine computer, answers. *badTester when it refused the tester address.
bool DiagService::proves(uint32_t address, const char *why, bool *badTester) {
    static const uint8_t kPresent[] = {0x3E, 0x00};
    const uint8_t targets[] = {kGatewayEcu, kEngineEcu};
    const int waits[] = {kAnswerMs, kRouteMs};
    char name[16], tester[24];
    *badTester = false;
    testerText(tester, sizeof(tester));
    for (size_t i = 0; i < sizeof(targets) && hsfz_.connected(); ++i) {
        uint8_t answer[16];
        size_t size = 0;
        const diag::HsfzTester::Result result = hsfz_.request(
            targets[i], kPresent, sizeof(kPresent), answer, sizeof(answer), &size, waits[i]);
        const unsigned refusal = result == diag::HsfzTester::Refused ? hsfz_.refusal() : 0;
        nbt_logf("host %s: diag: %s (%s) takes the connection; tester present to 0x%02x from "
                 "%s: %s (refusal 0x%04x)",
                 app_, text(address, name), why, static_cast<unsigned>(targets[i]), tester,
                 result == diag::HsfzTester::Ok        ? "answered"
                 : result == diag::HsfzTester::Refused ? "refused"
                 : result == diag::HsfzTester::Timeout ? "no answer"
                                                       : "connection lost",
                 refusal);
        if (result == diag::HsfzTester::Ok) {
            return true;
        }
        if (refusal == 0x0040 || refusal == 0x8302) { // HSFZ, DoIP: unknown source address
            *badTester = true;
            return false;
        }
        if (result == diag::HsfzTester::Broken) {
            return false;
        }
    }
    return false;
}

// 2: the gateway (or a host forwarding to the engine computer); 1: takes the
// connection, not proven; 0: nobody there.
int DiagService::tryGateway(uint32_t address, uint16_t port, bool doip, const char *why,
                            bool verbose) {
    char name[16];
    int took = 0;
    hsfz_.setProtocol(doip ? diag::HsfzTester::Doip : diag::HsfzTester::Hsfz);
    const size_t testers = doip ? sizeof(diag::kDoipTesters) / sizeof(diag::kDoipTesters[0])
                                : sizeof(diag::kTesterAddresses);
    for (size_t t = 0; t < testers; ++t) {
        if (doip) {
            hsfz_.setDoipTester(diag::kDoipTesters[t]);
        } else {
            hsfz_.setTester(diag::kTesterAddresses[t]);
        }
        if (!hsfz_.connect(address, port, kTryMs)) {
            if (doip && hsfz_.activation() >= 0) {
                took = 1;
                nbt_logf("host %s: diag: %s (%s) refuses DoIP tester 0x%04X (routing activation "
                         "0x%02X)",
                         app_, text(address, name), why,
                         static_cast<unsigned>(diag::kDoipTesters[t]),
                         static_cast<unsigned>(hsfz_.activation()));
                if (hsfz_.activation() == 0x00) { // unknown source address: try another
                    continue;
                }
            } else if (verbose && !t) {
                nbt_logf("host %s: diag: %s (%s) does not take %s on port %u (errno %d)", app_,
                         text(address, name), why, doip ? "DoIP" : "HSFZ",
                         static_cast<unsigned>(port), hsfz_.connectError());
            }
            break;
        }
        took = 1;
        bool badTester = false;
        if (proves(address, why, &badTester)) {
            return 2;
        }
        hsfz_.close();
        if (!badTester) {
            break;
        }
    }
    hsfz_.close();
    hsfz_.setProtocol(diag::HsfzTester::Hsfz);
    hsfz_.setTester(diag::kTesterAddress);
    hsfz_.setDoipTester(diag::kDoipTesters[0]);
    return took;
}

void DiagService::remember() {
    char line[64], name[16];
    const bool doip = hsfz_.protocol() == diag::HsfzTester::Doip;
    const int n =
        doip ? snprintf(line, sizeof(line), "%s:%u %04X doip\n", text(ip_, name),
                        static_cast<unsigned>(port_), static_cast<unsigned>(hsfz_.doipTester()))
             : snprintf(line, sizeof(line), "%s:%u %02X\n", text(ip_, name),
                        static_cast<unsigned>(port_), hsfz_.tester());
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line) ||
        nbt_fs_replace_text(fs_, NBT_FS_DIAG_GATEWAY_NEXT, NBT_FS_DIAG_GATEWAY, "manager", line,
                            static_cast<size_t>(n))) {
        nbt_logf("host %s: diag: cannot remember the gateway (errno %d)", app_, errno);
    }
}

bool DiagService::remembered(char *gateway, size_t size) {
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
    // "address:port tester" (HSFZ) or "address:port tester doip": a tester
    // address the search tries, the usual one otherwise.
    char address[48], protocol[8] = "";
    unsigned value = 0;
    uint32_t ip = 0;
    uint16_t port = 0;
    if (sscanf(line, "%47s %x %7s", address, &value, protocol) < 1 ||
        !diag::parseGateway(address, &ip, &port) || strlen(address) >= size) {
        return false;
    }
    const bool doip = !strcmp(protocol, "doip");
    hsfz_.setProtocol(doip ? diag::HsfzTester::Doip : diag::HsfzTester::Hsfz);
    for (size_t t = 0; t < sizeof(diag::kTesterAddresses); ++t) {
        if (!doip && diag::kTesterAddresses[t] == value) {
            hsfz_.setTester(static_cast<uint8_t>(value));
        }
    }
    for (size_t t = 0; t < sizeof(diag::kDoipTesters) / sizeof(diag::kDoipTesters[0]); ++t) {
        if (doip && diag::kDoipTesters[t] == value) {
            hsfz_.setDoipTester(static_cast<uint16_t>(value));
        }
    }
    memcpy(gateway, address, strlen(address) + 1);
    return true;
}

} // namespace process
} // namespace nbt
