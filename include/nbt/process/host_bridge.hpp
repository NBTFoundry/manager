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
 *    Direct group on);
 *  - `diag` (1.8): the application's socket (the child's fd 10) and the
 *    service answering it (process/diag_service.hpp); a policy that may clear
 *    fault codes adds driving.speedActual to the car data it asks for;
 *  - `companion` (1.9): the page file it creates (the child's fd 11), and
 *    the phone's events, both removed at the start and the end. */

#include "nbt/app_settings.h"
#include "nbt/bridge.h"
#include "nbt/bridge_hold.h"
#include "nbt/cds_policy.h"
#include "nbt/fs.h"
#include "nbt/net_policy.h"
#include "nbt/process/diag_service.hpp"

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
    int companionFd() const {
        return companionFd_;
    }
    /* The application's end of the diag socket, or -1. */
    int diagFd() const {
        return diagChild_;
    }
    void poll(uint64_t nowMs, bool ready);
    void stop();

private:
    HostBridge(const HostBridge &);
    HostBridge &operator=(const HostBridge &);
    void relay(uint64_t nowMs, nbt_hold_word word);
    void prepareDiag(const nbt_bridge_requirement &need, const char *grantCaps,
                     nbt_app_environment *environment);

    nbt_fs *fs_;
    char app_[NBT_HOLD_APP_MAX + 1];
    const char *dir_;
    int holdFd_, diagChild_, companionFd_;
    bool carData_, netOpen_, haveRequest_, relayed_, wantSpeed_;
    char cds_[NBT_CDS_POLICY_MAX + 1];
    DiagService diag_;
    char net_[NBT_NET_POLICY_MAX + 1];
    uint64_t requestSequence_, relayMs_, recordMs_;
    nbt_hold_word requestWord_;
};

} // namespace process
} // namespace nbt

#endif
