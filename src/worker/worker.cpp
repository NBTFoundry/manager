// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/worker.hpp"

#include "nbt/app_faults.h"
#include "nbt/app_host.h"
#include "nbt/app_provision.h"
#include "nbt/app_remove.h"
#include "nbt/bridge.h"
#include "nbt/core/system.hpp"
#include "nbt/fs.h"
#include "nbt/lifecycle/transactions.hpp"
#include "nbt/log.h"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {

WorkerState::WorkerState()
    : ended(Ended::None), generic(false), newApp(false), pending(), app(), hostApp(), hostStopMs(0),
      hostKilled(false), message(), installStartedMs(0), updateLock(), previouslyRunning(false),
      settingsFailed(false), recovery(false), approved(), op(Operation::None), state(State::Idle) {
}

Worker::Worker(storage::ManagedRoot &root, process::ChildRunner &children,
               process::HostControl &hosts)
    : root_(root), children_(children), hosts_(hosts), state_(), shutdown_(false), job_(), task_(),
      taskLease_() {
    clearTask(task_);
}

Worker::~Worker() {
    shutdown();
}

core::Status Worker::fail(const char *message) {
    snprintf(state_.message, sizeof(state_.message), "%s", message);
    state_.state = State::Failed;
    return core::Status::error(EIO, "worker operation failed");
}

core::Result<bool> Worker::running(const char *app) {
    if (!nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager")) {
        return core::Result<bool>::failure(core::Status::error(EINVAL, "invalid application"));
    }
    core::Result<pid_t> pid = hosts_.pid(root_, app);
    if (!pid.get()) {
        return core::Result<bool>::failure(pid.status());
    }
    return core::Result<bool>::success(pid.value() > 0);
}

core::Status Worker::selectApp(const char *app) {
    if (!nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager") || busy() ||
        state_.updateLock.valid() ||
        (state_.session.active(0) && state_.app[0] && strcmp(state_.app, app)) ||
        (state_.child.running() && strcmp(state_.app, app))) {
        return fail("application unavailable");
    }
    if (state_.child.running()) {
        return core::Status();
    }
    core::Result<bool> isRunning = running(app);
    if (isRunning.get() && isRunning.value()) {
        core::Result<nbt_installed> record = lifecycle::AppTransactions::installed(root_, app);
        if (!record.get()) {
            return fail("application baseline failed");
        }
        memcpy(state_.app, app, strlen(app) + 1);
        return core::Status();
    }
    core::UniqueFd lock(nbt_tx_lock(root_.legacy(), app));
    if (!lock.valid()) {
        return fail("application is busy");
    }
    core::Result<core::UniqueFd> lease = root_.lease(app);
    if (!lease.get()) {
        return fail("application is still running");
    }
    char error[128];
    const int result = nbt_tx_recover(root_.legacy(), app, error, sizeof(error));
    (void)lease.value().reset();
    lock.reset();
    core::Result<nbt_installed> record = lifecycle::AppTransactions::installed(root_, app);
    if (result || !record.get()) {
        return fail("application recovery or baseline failed");
    }
    memcpy(state_.app, app, strlen(app) + 1);
    return core::Status();
}

core::Status Worker::beginStop(Operation op, uint64_t nowMs) {
    if (state_.child.running()) {
        core::Status status = children_.terminate(state_.child, nowMs);
        if (!status.ok()) {
            return status;
        }
    }
    state_.op = op;
    return core::Status();
}

void Worker::releaseUpdateLock() {
    state_.updateLock.reset();
}

core::Status Worker::beginHostStop(const char *app, Operation op, uint64_t nowMs) {
    core::Status status = hosts_.signal(root_, app, SIGTERM);
    if (!status.ok()) {
        return status;
    }
    memcpy(state_.hostApp, app, strlen(app) + 1);
    state_.hostStopMs = nowMs;
    state_.hostKilled = false;
    state_.op = op;
    return core::Status();
}

core::Status Worker::hostStart() {
    return hosts_.start(root_, state_.app, false);
}

core::Result<StartResult> Worker::start(const char *app, uint64_t nowMs) {
    (void)nowMs;
    if (busy() || state_.op != Operation::None || state_.child.running()) {
        return core::Result<StartResult>::failure(
            core::Status::error(EBUSY, "application unavailable"));
    }
    watchEnd();
    core::Result<bool> isRunning = running(app);
    if (!isRunning.get()) {
        nbt_logf("start %s: %s (errno %d)", app, isRunning.status().message(),
                 isRunning.status().code());
        fail("cannot start application");
        return core::Result<StartResult>::failure(
            core::Status::error(EIO, "cannot start application"));
    }
    if (isRunning.value()) {
        core::Result<bool> shown = hosts_.show(root_, app);
        if (!shown.get()) {
            fail("cannot show running application");
            return core::Result<StartResult>::failure(shown.status());
        }
        snprintf(state_.message, sizeof(state_.message), "%s",
                 shown.value() ? "Application already running" : "Application show requested");
        if (shown.value()) {
            return core::Result<StartResult>::success(StartResult::AlreadyRunning);
        }
        const core::Result<pid_t> host = hosts_.pid(root_, app);
        const pid_t owner = nbt_app_surface_owner(root_.legacy());
        return core::Result<StartResult>::success(host.get() && owner > 0 && owner == host.value()
                                                      ? StartResult::Screen
                                                      : StartResult::Started);
    }
    nbt_installed record;
    char data[NBT_PATH_MAX];
    if (nbt_app_fault_quarantined(root_.legacy(), app)) {
        snprintf(state_.message, sizeof(state_.message), "Application blocked after faults");
        return core::Result<StartResult>::success(StartResult::Quarantined);
    }
    errno = 0; // a check that sets none must not show an earlier call's value
    const char *unusable = !selectApp(app).ok()                             ? "unavailable"
                           : nbt_tx_installed(root_.legacy(), app, &record) ? "record unreadable"
                           : nbt_fs_app_data(root_.legacy(), app, data)     ? "data folder unusable"
                                                                            : NULL;
    if (unusable) {
        nbt_logf("start %s: application %s (errno %d)", app, unusable, errno);
        fail("cannot start application");
        return core::Result<StartResult>::failure(
            core::Status::error(EIO, "cannot start application"));
    }
    nbt_bridge_provider provider;
    nbt_bridge_version grant;
    char error[128];
    nbt_bridge_current(&provider);
    if (nbt_bridge_check(&record.bridge, &provider, provider.versions[0].caps, &grant, error,
                         sizeof(error))) {
        // Disabled until an update makes it compatible (app_compat.h): never started.
        nbt_logf("start %s: needs an update: %s", app, error);
        snprintf(state_.message, sizeof(state_.message), "Application needs an update");
        return core::Result<StartResult>::success(StartResult::Incompatible);
    }
    // One application at a time has the surface the GUI can put on the panel.
    const bool screen = nbt_bridge_cap_has(grant.caps, "show");
    if (screen && nbt_app_surface_owner(root_.legacy()) != 0) {
        snprintf(state_.message, sizeof(state_.message), "Another application has the screen");
        return core::Result<StartResult>::success(StartResult::ScreenTaken);
    }
    const core::Status started = hostStart();
    if (!started.ok()) {
        nbt_logf("start %s: %s (errno %d)", app, started.message(), started.code());
        fail("cannot start application");
        return core::Result<StartResult>::failure(
            core::Status::error(EIO, "cannot start application"));
    }
    snprintf(state_.message, sizeof(state_.message), "Application started");
    return core::Result<StartResult>::success(screen ? StartResult::Screen : StartResult::Started);
}

core::Status Worker::stop(const char *app, uint64_t nowMs) {
    if (busy() || state_.op != Operation::None) {
        return core::Status::error(EBUSY, "application unavailable");
    }
    if (!app) {
        app = state_.app;
    }
    watchEnd();
    if (!app[0]) {
        snprintf(state_.message, sizeof(state_.message), "Application stopped");
        return core::Status();
    }
    core::Result<bool> isRunning = running(app);
    if (!isRunning.get()) {
        return fail("cannot stop application");
    }
    if (!isRunning.value()) {
        snprintf(state_.message, sizeof(state_.message), "Application stopped");
        return core::Status();
    }
    if (!beginHostStop(app, Operation::HostStop, nowMs).ok()) {
        return fail("cannot stop application");
    }
    snprintf(state_.message, sizeof(state_.message), "Stopping application");
    return core::Status();
}

core::Status Worker::approve(const unsigned char digest[32], bool recovery, uint64_t nowMs) {
    if (state_.managerUpdate) {
        return approveManager(digest, nowMs);
    }
    return state_.newApp ? installNew(digest, nowMs) : approveUpdate(digest, recovery, nowMs);
}

// A new application: nothing runs and nothing needs restoring, so it is
// installed at once from the session's upload, as a first package is
// provisioned, on the job's thread (installNewDone). Accepted approvals
// report their outcome through STATUS.
core::Status Worker::installNew(const unsigned char digest[32], uint64_t nowMs) {
    if (state_.state != State::Verified || state_.op != Operation::None || busy() ||
        !state_.session.active(nowMs) || !digest ||
        !core::constantEqual(core::Span<const uint8_t>(digest, 32),
                             core::Span<const uint8_t>(state_.pending.package_hash, 32))) {
        return core::Status::error(EACCES, "approval rejected");
    }
    sessionEnd(nowMs, Ended::None);
    state_.state = State::Installing;
    snprintf(state_.message, sizeof(state_.message), "Installing application");
    clearTask(task_);
    task_.task = Task::InstallNew;
    snprintf(task_.app, sizeof(task_.app), "%s", state_.app);
    memcpy(task_.digest, digest, sizeof(task_.digest));
    core::Result<core::UniqueFd> upload =
        root_.open(NBT_FS_SESSION_UPLOAD, "manager", NULL, NBT_FS_READ);
    if (upload.get()) {
        task_.fd = upload.value().release();
    }
    startTask(nowMs);
    return core::Status();
}

void Worker::installNewDone() {
    (void)removeUpload();
    state_.newApp = false;
    if (task_.result) {
        nbt_logf("new application %s: %s", state_.app, task_.error);
        fail("Installation failed");
        return;
    }
    (void)nbt_app_fault_clear(root_.legacy(), state_.app);
    state_.state = State::Installed;
    snprintf(state_.message, sizeof(state_.message), "Application installed");
}

core::Result<int> Worker::remove(const char *app, uint64_t nowMs) {
    if (!nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager")) {
        return core::Result<int>::failure(core::Status::error(EINVAL, "invalid application"));
    }
    if (busy() || state_.op != Operation::None || state_.updateLock.valid() ||
        state_.child.running() || state_.session.active(nowMs)) {
        return core::Result<int>::success(NBT_APP_REMOVE_BUSY);
    }
    watchEnd(); // as START and STOP: the last session's status access ends
    const int result = nbt_app_remove(root_.legacy(), app);
    if (result < 0) {
        return core::Result<int>::failure(core::Status::fromErrno("cannot remove application"));
    }
    if (result == NBT_APP_REMOVED && !strcmp(state_.app, app)) {
        state_.app[0] = 0;
    }
    return core::Result<int>::success(result);
}

} // namespace worker
} // namespace nbt
