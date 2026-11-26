// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_IPC_DISPATCHER_HPP
#define NBT_WORKER_IPC_DISPATCHER_HPP

#include "nbt/package.h"
#include "nbt/storage/managed_root.hpp"
#include "nbt/worker/control_protocol.hpp"
#include "nbt/worker/ipc_files.hpp"
#include "nbt/worker/self_test.hpp"
#include "nbt/worker/web.hpp"
#include "nbt/worker/wifi_direct_job.hpp"
#include "nbt/worker/worker.hpp"

#include <stdint.h>

namespace nbt {
namespace worker {

class IpcDispatcher {
public:
    IpcDispatcher(storage::ManagedRoot &root, Worker &worker, WebServer &web, WifiDirectJob &wfd,
                  ControlProtocol &control);
    IpcDispatcher(const IpcDispatcher &) = delete;
    IpcDispatcher &operator=(const IpcDispatcher &) = delete;

    core::Status dispatch(char *line, uint64_t nowMs);
    /* SESSION's reply once its Wi-Fi Direct preparation ended (ipc_session.cpp),
     * SETUP's and SELFTEST PREPARE's once the worker's job ran them
     * (ipc_system.cpp, ipc_selftest.cpp). */
    core::Status poll(uint64_t nowMs);
    bool replyPending() const {
        return sessionPending_ || setupPending_ || selfTestPending_;
    }

private:
    core::Status handlePhone(const char *command, uint64_t now);
    core::Status handleStatus();
    core::Status handleList();
    core::Status handleRunning(char *cursor);
    core::Status handleServices();
    core::Status handleAutostart(char *cursor);
    core::Status handleSettings(bool setting, char *cursor);
    core::Status handleStart(char *cursor, uint64_t nowMs);
    core::Status handleStopRemove(bool removing, const char *app, bool fault, uint64_t nowMs);
    core::Status handleUnblock(const char *app);
    core::Status handleSession(char *cursor, uint64_t nowMs);
    core::Status handleApprove(char *cursor, uint64_t nowMs);
    core::Status handleSystem(bool setup, const char *cursor);
    core::Status handleSsh(const char *cursor);
    core::Status handleSelfTest(const char *cursor, uint64_t nowMs);
    core::Status finishSelfTest();
    core::Status finishSession(bool prepared, ReadyWifiCredentials &wifi, uint64_t nowMs);
    core::Status finishSetup();

    storage::ManagedRoot &root_;
    Worker &worker_;
    WebServer &web_;
    WifiDirectJob &wfd_;
    ControlProtocol &control_;
    IpcFiles files_;              // FILES (ipc_files.hpp)
    bool sessionPending_ = false; // SESSION answered once Wi-Fi Direct is prepared
    char sessionApp_[NBT_ID_MAX + 1] = {};
    char sessionIface_[32] = {};
    unsigned sessionPort_ = 0;
    bool setupPending_ = false;     // SETUP answered once its job ended
    bool selfTestPending_ = false;  // SELFTEST PREPARE answered once its job ended
    bool selfTestPrepared_ = false; // prepared_ holds the last preparation, for REPORT
    SelfTestPrepared prepared_ = SelfTestPrepared();
};

} // namespace worker
} // namespace nbt

#endif
