#ifndef NBT_WORKER_WFD_HEALTH_HPP
#define NBT_WORKER_WFD_HEALTH_HPP

#include "nbt/worker/wifi_direct_most.hpp"

#include <stddef.h>

namespace nbt {
namespace worker {

/* Debug-log lines that tell why a phone cannot join the group (dev67: the
 * phone associated and dropped 4 to 8 s later). No secret is logged: only
 * whether the passphrase from MOST equals WFDPass.conf's, whether
 * Connectivity's dhcpd for wfd0 runs (pid file /tmp/dhcpd.pid.wfd0, as the
 * car's process list shows it), and how many leases its file holds. */

unsigned countDhcpLeases(const char *text, size_t size);

struct WfdHealth {
    WfdHealth() : leasesAtStart(-1), active(false) {
    }
    int leasesAtStart; // -1: no leases file
    bool active;
};

void logWfdReady(const WfdConfig &cfg, WfdHealth *health);
void logWfdSessionEnd(WfdHealth *health);

} // namespace worker
} // namespace nbt

#endif
