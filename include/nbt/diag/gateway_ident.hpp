// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DIAG_GATEWAY_IDENT_HPP
#define NBT_DIAG_GATEWAY_IDENT_HPP

#include "nbt/diag/hsfz_tester.hpp"
#include "nbt/diag/link_facts.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace diag {

static const uint16_t kIdentPort = 6811;

/* Vehicle identification, as testers find a car's gateway: on each link, from
 * the link's address to its broadcast address, the HSFZ request (an empty
 * frame, control 0x0011, UDP 6811) and the DoIP one (payload type 0x0001,
 * UDP 13400). Whoever answers within `timeoutMs` is in `out`, once per
 * protocol, with the address its answer names: HSFZ "DIAGADR10", DoIP its
 * logical address (0x0010: the gateway; 0xFFFF when none). `sendError[i]`
 * gets the errno of an HSFZ request that could not leave (the head unit's
 * packet filter blocks it on dwc0), else 0. Returns how many answered. */
struct Identified {
    uint32_t address;
    uint16_t diagAddress;
    size_t link;
    bool doip;
};
/* The DoIP port, TCP and UDP (NBT_TEST_DIAG_DOIP_PORT in tests). */
uint16_t doipPort();
size_t identifyGateways(const LinkFacts *links, size_t count, int *sendError, int timeoutMs,
                        Identified *out, size_t max);

} // namespace diag
} // namespace nbt

#endif
