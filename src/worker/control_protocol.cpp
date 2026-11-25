// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/control_protocol.hpp"

#include "nbt/app_compat.h"
#include "nbt/app_faults.h"
#include "nbt/app_host.h"
#include "nbt/core/checked.hpp"
#include "nbt/core/span.hpp"
#include "nbt/core/system.hpp"
#include "nbt/core/text.hpp"
#include "nbt/fs.h"
#include "nbt/log.h"
#include "nbt/permissions.h"
#include "nbt/transaction.h"
#include "nbt/util.h"
#include "nbt/worker/ipc_dispatcher.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

bool terminateLine(char *list, size_t capacity, size_t used) {
    if (used + 2 > capacity) {
        return false;
    }
    list[used] = '\n';
    list[used + 1] = 0;
    return true;
}

} // namespace

ControlProtocol::ControlProtocol(int fd)
    : fd_(fd), quitting_(false), negotiated_(false), inputSize_(0) {
}

core::Status ControlProtocol::respond(const char *message) {
    if (!message) {
        return core::Status::error(EINVAL, "missing control response");
    }
    core::Result<uint64_t> now = core::monotonicMs();
    if (!now.get()) {
        return now.status();
    }
    uint64_t deadline;
    if (!core::checkedAdd(now.value(), UINT64_C(15000), &deadline)) {
        return core::Status::error(ERANGE, "control deadline overflow");
    }
    return core::writeWithDeadline(
        fd_, core::Span<const uint8_t>(reinterpret_cast<const uint8_t *>(message), strlen(message)),
        deadline);
}

core::Status ControlProtocol::read(ControlHandler &handler, uint64_t nowMs) {
    if (inputSize_ >= sizeof(input_)) {
        return core::Status::error(EOVERFLOW, "control input too large");
    }
    const ssize_t count = ::read(fd_, input_ + inputSize_, sizeof(input_) - inputSize_);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        return core::Status();
    }
    if (count <= 0) {
        return core::Status::error(ECONNRESET, "control channel closed");
    }
    inputSize_ += static_cast<size_t>(count);
    return process(handler, nowMs);
}

core::Status ControlProtocol::process(ControlHandler &handler, uint64_t nowMs) {
    size_t start = 0;
    for (size_t i = 0; i < inputSize_ && !handler.replyPending(); ++i) {
        const unsigned char c = static_cast<unsigned char>(input_[i]);
        if (c == '\n') {
            input_[i] = 0;
            core::Status status = handler.command(input_ + start, nowMs);
            if (!status.ok()) {
                return status;
            }
            start = i + 1;
        } else if (c < 32 || c > 126) {
            return core::Status::error(EINVAL, "invalid control byte");
        }
    }
    if (start) {
        memmove(input_, input_ + start, inputSize_ - start);
        inputSize_ -= start;
    }
    return inputSize_ == sizeof(input_)
               ? core::Status::error(EOVERFLOW, "control command too large")
               : core::Status();
}

core::Status IpcDispatcher::handleStatus() {
    nbt_installed installed;
    char hash[65] = "-";
    char output[512];
    const char *version = "-";
    const char *pending = "-";
    unsigned long long sequence = 0;
    const WorkerState &state = worker_.state();
    int stateNumber = static_cast<int>(state.state);
    if (state.state == State::Session && state.session.opened()) {
        stateNumber = 6;
    }
    const char *app = state.generic || !state.app[0] ? "-" : state.app;
    if (state.managerUpdate) {
        version = state.managerInstalled;
    } else if (strcmp(app, "-") && !nbt_tx_installed(root_.legacy(), state.app, &installed)) {
        version = installed.version;
    }
    if (state.state == State::Verified || state.state == State::Installing) {
        pending = state.pending.version;
        sequence = static_cast<unsigned long long>(state.pending.sequence);
        if (!core::hexEncode(core::Span<const uint8_t>(state.pending.package_hash, 32),
                             core::Span<char>(hash, sizeof(hash)))
                 .ok()) {
            return core::Status::error(EIO, "cannot format package hash");
        }
    }
    // The pending package's ninth field names the developer whose key signed
    // it, a tenth its permissions and those an update adds (docs/permissions.md).
    const bool reviewed = state.state == State::Verified || state.state == State::Installing;
    const bool developer = reviewed && state.developer[0];
    const bool permissions = reviewed && state.permissions;
    char tenth[24] = "\t";
    if (!permissions ||
        nbt_permissions_format(state.permissions, state.newPermissions, tenth + 1, 23) < 0) {
        tenth[0] = 0;
    }
    const int length =
        snprintf(output, sizeof(output), "STATUS\t%d\t%s\t%s\t%s\t%llu\t%s\t%s%s%s%s\n",
                 stateNumber, app, version, pending, sequence, hash,
                 state.message[0] ? state.message : "-", developer || permissions ? "\t" : "",
                 developer     ? state.developer
                 : permissions ? "-"
                               : "",
                 tenth);
    return length < 0 || length >= static_cast<int>(sizeof(output))
               ? core::Status::error(ERANGE, "status response too large")
               : control_.respond(output);
}

core::Status IpcDispatcher::handleStopRemove(bool removing, const char *app, bool fault,
                                             uint64_t nowMs) {
    if (!removing) {
        const core::Status stopped = worker_.stop(app, nowMs);
        if (stopped.ok() && fault && app) {
            (void)nbt_app_fault_record(root_.legacy(), app);
        }
        return control_.respond(stopped.ok() ? "OK\n" : "ERR stop\n");
    }
    if (!app) {
        return control_.respond("ERR invalid-command\n");
    }
    // In NBT_APP_REMOVE*'s order: removed, running, busy, missing.
    static const char *const kReplies[] = {"OK\n", "ERR running\n", "ERR busy\n", "ERR missing\n"};
    static const char *const kLogs[] = {"uninstalled", "refused, it is running",
                                        "refused, the manager is busy", "not installed"};
    const core::Result<int> removed = worker_.remove(app, nowMs);
    if (!removed.get()) {
        nbt_logf("uninstall %s: failed (errno %d)", app, removed.status().code());
        return control_.respond(removed.status().code() == EINVAL ? "ERR invalid-command\n"
                                                                  : "ERR remove\n");
    }
    nbt_logf("uninstall %s: %s", app, kLogs[removed.value()]);
    return control_.respond(kReplies[removed.value()]);
}

core::Status IpcDispatcher::handleUnblock(const char *app) {
    if (!app || !nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager") ||
        nbt_fs_exists(root_.legacy(), NBT_FS_PROFILE, app, NULL) != 1) {
        return control_.respond("ERR invalid-command\n");
    }
    return control_.respond(nbt_app_fault_clear(root_.legacy(), app) ? "ERR unblock\n" : "OK\n");
}

core::Status IpcDispatcher::handleList() {
    char names[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1];
    char list[1024];
    const int count = nbt_fs_list_apps(root_.legacy(), names);
    if (count < 0) {
        return control_.respond("ERR list\n");
    }
    int length = snprintf(list, sizeof(list), "APPS\t%d", count);
    if (length < 0 || length >= static_cast<int>(sizeof(list))) {
        return core::Status::error(ERANGE, "list too large");
    }
    size_t used = static_cast<size_t>(length);
    for (int i = 0; i < count; ++i) {
        // "|update": disabled until updated (app_compat.h); an unreadable record
        // is listed as is, and starting it says so.
        const bool update = nbt_app_compatible(root_.legacy(), names[i], NULL, 0) == 1;
        length =
            snprintf(list + used, sizeof(list) - used, "\t%s%s", names[i], update ? "|update" : "");
        if (length < 0 || static_cast<size_t>(length) >= sizeof(list) - used) {
            return core::Status::error(ERANGE, "list too large");
        }
        used += static_cast<size_t>(length);
    }
    if (!terminateLine(list, sizeof(list), used)) {
        return core::Status::error(ERANGE, "list too large");
    }
    return control_.respond(list);
}

// The applications whose service runs (docs/service-bridge.md), as RUNNING names them.
core::Status IpcDispatcher::handleServices() {
    char names[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1];
    char list[1024];
    const int count = nbt_fs_list_apps(root_.legacy(), names);
    if (count < 0) {
        return control_.respond("ERR list\n");
    }
    int running = 0;
    for (int i = 0; i < count; ++i) {
        core::Result<bool> service = worker_.serviceRunning(names[i]);
        if (service.get() && service.value()) {
            memmove(names[running++], names[i], sizeof(names[i]));
        }
    }
    int length = snprintf(list, sizeof(list), "SERVICES\t%d", running);
    size_t used = length > 0 ? static_cast<size_t>(length) : 0;
    for (int i = 0; length >= 0 && i < running; ++i) {
        length = snprintf(list + used, sizeof(list) - used, "\t%s", names[i]);
        if (length < 0 || static_cast<size_t>(length) >= sizeof(list) - used) {
            length = -1;
        } else {
            used += static_cast<size_t>(length);
        }
    }
    if (length < 0 || !terminateLine(list, sizeof(list), used)) {
        return core::Status::error(ERANGE, "list too large");
    }
    return control_.respond(list);
}

core::Status IpcDispatcher::handleRunning(char *cursor) {
    char names[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1];
    char list[2048];
    const bool withStatus = cursor != NULL;
    const int count = nbt_fs_list_apps(root_.legacy(), names);
    int runningCount = 0;
    if (count < 0) {
        return control_.respond("ERR list\n");
    }
    for (int i = 0; i < count; ++i) {
        core::Result<bool> running = worker_.running(names[i]);
        if (running.get() && running.value()) {
            memmove(names[runningCount++], names[i], sizeof(names[i]));
        }
    }
    int length = snprintf(list, sizeof(list), "RUNNING\t%d", runningCount);
    if (length < 0 || length >= static_cast<int>(sizeof(list))) {
        return core::Status::error(ERANGE, "list too large");
    }
    size_t used = static_cast<size_t>(length);
    for (int i = 0; i < runningCount; ++i) {
        char appStatus[NBT_BRIDGE_STATUS_MAX + 1] = "";
        if (withStatus) {
            (void)nbt_app_host_status(root_.legacy(), names[i], appStatus);
        }
        length = snprintf(list + used, sizeof(list) - used, "\t%s%s%s", names[i],
                          appStatus[0] ? "|" : "", appStatus);
        if (length < 0 || static_cast<size_t>(length) >= sizeof(list) - used) {
            return core::Status::error(ERANGE, "list too large");
        }
        used += static_cast<size_t>(length);
    }
    if (!terminateLine(list, sizeof(list), used)) {
        return core::Status::error(ERANGE, "list too large");
    }
    return control_.respond(list);
}

} // namespace worker
} // namespace nbt
