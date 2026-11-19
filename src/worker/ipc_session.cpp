// SPDX-License-Identifier: MPL-2.0
/* SESSION's reply. On wfd0 the Wi-Fi Direct group is prepared first on its own
 * thread (WifiDirectJob, up to 10 s); the worker's loop keeps serving meanwhile
 * and the GUI's later commands wait (ControlHandler::replyPending). */
#include "nbt/core/secure.hpp"
#include "nbt/log.h"
#include "nbt/worker/ipc_dispatcher.hpp"
#include "nbt/worker/wifi_direct.hpp"

#include <errno.h>

namespace nbt {
namespace worker {

core::Status IpcDispatcher::poll(uint64_t nowMs) {
    if (setupPending_) {
        return finishSetup();
    }
    bool prepared = false;
    ReadyWifiCredentials wifi;
    if (!sessionPending_ || !wfd_.finish(&prepared, &wifi)) {
        return core::Status();
    }
    sessionPending_ = false;
    return finishSession(prepared, wifi, nowMs);
}

core::Status IpcDispatcher::finishSession(bool prepared, ReadyWifiCredentials &wifi,
                                          uint64_t nowMs) {
    // A preparation runs on the Wi-Fi Direct job's thread and logs its own
    // cause: this thread's errno says nothing about it.
    const core::Status listening =
        prepared ? web_.listen(sessionIface_, sessionPort_) : core::Status();
    if (!prepared || !listening.ok()) {
        if (prepared) {
            nbt_logf("session: cannot listen on %s: %s (errno %d)", sessionIface_,
                     listening.message(), listening.code());
        } else {
            nbt_logf("session: %s not prepared", sessionIface_);
        }
        wifi.clear();
        wfd_.restore(nowMs);
        worker_.cancel(nowMs);
        return control_.respond("ERR network-unavailable\n");
    }
    nbt_logf("session for %s listening on %s", sessionApp_[0] ? sessionApp_ : "any application",
             web_.host());
    char output[384];
    const bool built = wifi.managed ? sessionReply(output, sizeof(output), web_.host(), wifi.ssid,
                                                   wifi.pass.data())
                                    : sessionReply(output, sizeof(output), web_.host());
    wifi.clear();
    const core::Status status = built ? control_.respond(output)
                                      : core::Status::error(ERANGE, "session response too large");
    core::secureClear(output, sizeof(output));
    return status;
}

} // namespace worker
} // namespace nbt
