// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_HOST_BRIDGE_HPP
#define NBT_PROCESS_HOST_BRIDGE_HPP

/* A host's part of bridge 1.6 for one user launch (docs/app-bridge.md):
 *  - `hold`: the request file it creates (the child's fd 9), relayed to the
 *    iDrive plugin once the child is ready, refreshed every second, released
 *    when the child ends;
 *  - `car-data`: the record of the names the application declared, refreshed
 *    every second while it runs, which the plugin subscribes to;
 *  - `net`: the packet-filter rules it declared, opened before the start and
 *    closed at its end, with a record saying so (a wfd0 rule keeps the Wi-Fi
 *    Direct group on). */

#include "nbt/app_settings.h"
#include "nbt/bridge.h"
#include "nbt/bridge_hold.h"
#include "nbt/cds_policy.h"
#include "nbt/fs.h"
#include "nbt/net_policy.h"

#include <stdint.h>

namespace nbt {
namespace process {

class HostBridge {
public:
    HostBridge();
    ~HostBridge();
    /* Before the spawn, by grant. What cannot be provided is left out, and its
     * variables taken out of `environment`, as for the status channel. */
    void prepare(nbt_fs *fs, const char *app, const char *grantCaps,
                 const nbt_bridge_requirement &need, nbt_app_environment *environment);
    int holdFd() const {
        return holdFd_;
    }
    void poll(uint64_t nowMs, bool ready);
    void stop();

private:
    HostBridge(const HostBridge &);
    HostBridge &operator=(const HostBridge &);
    void relay(uint64_t nowMs, nbt_hold_word word);

    nbt_fs *fs_;
    char app_[NBT_HOLD_APP_MAX + 1];
    const char *dir_;
    int holdFd_;
    bool carData_, netOpen_, haveRequest_, relayed_;
    char cds_[NBT_CDS_POLICY_MAX + 1];
    char net_[NBT_NET_POLICY_MAX + 1];
    uint64_t requestSequence_, relayMs_, recordMs_;
    nbt_hold_word requestWord_;
};

} // namespace process
} // namespace nbt

#endif
