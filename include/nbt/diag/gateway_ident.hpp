// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DIAG_GATEWAY_IDENT_HPP
#define NBT_DIAG_GATEWAY_IDENT_HPP

#include "nbt/diag/link_facts.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace diag {

static const uint16_t kIdentPort = 6811;

/* HSFZ vehicle identification, as testers find a car's gateway: one request
 * (an empty frame, control 0x0011) to UDP port 6811 at the broadcast address
 * of each link, sent from the link's address; whoever answers within
 * `timeoutMs` is in `out`, with the diagnostic address its answer names
 * ("DIAGADR10": 0x10, the gateway; 0xFF when it names none). `sendError[i]`
 * gets the errno of a request that could not leave (the head unit's packet
 * filter blocks it on dwc0), else 0. Returns how many answered. */
struct Identified {
    uint32_t address;
    uint8_t diagAddress;
    size_t link;
};
size_t identifyGateways(const LinkFacts *links, size_t count, int *sendError, int timeoutMs,
                        Identified *out, size_t max);

} // namespace diag
} // namespace nbt

#endif
