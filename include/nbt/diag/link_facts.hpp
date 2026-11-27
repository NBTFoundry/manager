// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DIAG_LINK_FACTS_HPP
#define NBT_DIAG_LINK_FACTS_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace diag {

/* The head unit's link to the car's gateway (docs/engine-diagnostics.md). */
static const char kGatewayLink[] = "dwc0";
static const size_t kLinkCandidates = 8;

/* What the head unit knows of an interface, read from the system: its
 * address, and who could be the gateway there: the routers of its routes and
 * the neighbours it has talked to (its ARP table, the DHCP server that gave
 * the address among them). Addresses in network byte order. */
struct LinkFacts {
    bool present, up;
    uint32_t address, netmask;
    uint32_t candidates[kLinkCandidates];
    size_t count;
};

/* False when nothing can be read (not this system, no such interface). */
bool linkFacts(const char *interface, LinkFacts *out);
/* "169.254.199.119/255.255.0.0 up, neighbours 169.254.1.1 169.254.2.2" for the log. */
void linkFactsText(const LinkFacts &facts, char *out, size_t size);

} // namespace diag
} // namespace nbt

#endif
