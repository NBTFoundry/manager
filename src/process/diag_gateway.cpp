// SPDX-License-Identifier: MPL-2.0
/* Finding the car's gateway when the usual address does not answer
 * (docs/engine-diagnostics.md): its address differs between cars, the packet
 * filter stops the search by broadcast, and only the car can say what is on
 * that link. So the log gets what the head unit knows of it, and whoever the
 * head unit already talks to there is tried on the diagnostic port. */
#include "nbt/diag/link_facts.hpp"
#include "nbt/log.h"
#include "nbt/process/diag_service.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>

namespace nbt {
namespace process {
namespace {

const int kTryMs = 400, kAnswerMs = 600;
// The gateway's own diagnostic address: it answers a tester present there,
// another control unit on the link does not route it.
const uint8_t kGatewayEcu = 0x10;

const char *text(uint32_t address, char out[16]) {
    struct in_addr value;
    value.s_addr = address;
    strncpy(out, inet_ntoa(value), 15);
    out[15] = 0;
    return out;
}

} // namespace

bool DiagService::findGateway() {
    diag::LinkFacts facts;
    char link[240], name[16];
    const int error = hsfz_.connectError();
    if (!diag::linkFacts(diag::kGatewayLink, &facts)) {
        nbt_logf("host %s: diag: gateway %s:%u did not answer (errno %d); %s not read", app_,
                 text(ip_, name), static_cast<unsigned>(port_), error, diag::kGatewayLink);
        return false;
    }
    diag::linkFactsText(facts, link, sizeof(link));
    nbt_logf("host %s: diag: gateway %s:%u did not answer (errno %d); %s is %s", app_,
             text(ip_, name), static_cast<unsigned>(port_), error, diag::kGatewayLink, link);
    uint32_t open = 0; // the first that takes a connection, if none proves a gateway
    for (size_t i = 0; i < facts.count; ++i) {
        const uint32_t candidate = facts.candidates[i];
        if (candidate == ip_ || !hsfz_.connect(candidate, port_, kTryMs)) {
            continue;
        }
        const uint8_t present[] = {0x3E, 0x00};
        uint8_t answer[16];
        size_t size = 0;
        const diag::HsfzTester::Result result = hsfz_.request(
            kGatewayEcu, present, sizeof(present), answer, sizeof(answer), &size, kAnswerMs);
        nbt_logf("host %s: diag: %s takes the diagnostic port; tester present to 0x%02x: %s", app_,
                 text(candidate, name), static_cast<unsigned>(kGatewayEcu),
                 result == diag::HsfzTester::Ok ? "answered" : "no answer");
        if (result == diag::HsfzTester::Ok) {
            ip_ = candidate;
            nbt_logf("host %s: diag: gateway found at %s", app_, name);
            return true;
        }
        open = open ? open : candidate;
        hsfz_.close();
    }
    if (open && hsfz_.connect(open, port_, kTryMs)) {
        ip_ = open;
        nbt_logf("host %s: diag: using %s, the one address that takes the port", app_,
                 text(open, name));
        return true;
    }
    return false;
}

} // namespace process
} // namespace nbt
