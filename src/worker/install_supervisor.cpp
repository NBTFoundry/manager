// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/worker.hpp"

#include "nbt/app_faults.h"
#include "nbt/app_settings.h"
#include "nbt/core/system.hpp"
#include "nbt/core/text.hpp"
#include "nbt/lifecycle/app_settings.hpp"
#include "nbt/transaction.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <utility>

namespace nbt {
namespace worker {

void Worker::restoreFinish(uint64_t nowMs) {
    (void)nowMs;
    core::Result<core::UniqueFd> lease = root_.lease(state_.app);
    if (!lease.get()) {
        fail("application lease prevents recovery");
        return;
    }
    char error[128];
    const int result = nbt_tx_recover(root_.legacy(), state_.app, error, sizeof(error));
    (void)lease.value().reset();
    if (result) {
        fail("transaction recovery failed");
        return;
    }
    releaseUpdateLock();
    if (state_.previouslyRunning && !hostStart().ok()) {
        fail("previous application restored but cannot start");
        return;
    }
    nbt_installed record;
    char pendingHash[65];
    if (!nbt_tx_installed(root_.legacy(), state_.app, &record) &&
        core::hexEncode(core::Span<const uint8_t>(state_.pending.payload_hash, 32),
                        core::Span<char>(pendingHash, sizeof(pendingHash)))
            .ok() &&
        !strcmp(record.sha256, pendingHash)) {
        fail("Application recovered after commit");
        return;
    }
    fail("Update failed; previous application restored");
}

void Worker::restoreBegin(uint64_t nowMs) {
    snprintf(state_.message, sizeof(state_.message), "Restoring previous application");
    if (!state_.child.running()) {
        restoreFinish(nowMs);
        return;
    }
    if (!beginStop(Operation::Restore, nowMs).ok()) {
        fail("cannot stop candidate for recovery");
    }
}

core::Status Worker::approveUpdate(const unsigned char digest[32], bool recovery, uint64_t nowMs) {
    if (state_.state != State::Verified ||
        (state_.op != Operation::None && state_.op != Operation::HostStop) ||
        (state_.op != Operation::None && strcmp(state_.hostApp, state_.app)) ||
        !state_.session.active(nowMs) || !digest ||
        !core::constantEqual(core::Span<const uint8_t>(digest, 32),
                             core::Span<const uint8_t>(state_.pending.package_hash, 32))) {
        return core::Status::error(EACCES, "approval rejected");
    }
    memcpy(state_.approved.data(), digest, state_.approved.size());
    state_.recovery = recovery;
    sessionEnd(nowMs, Ended::None);
    core::Result<bool> isRunning = running(state_.app);
    state_.previouslyRunning =
        isRunning.get() && isRunning.value() && state_.op != Operation::HostStop;
    state_.updateLock =
        core::UniqueFd(isRunning.get() ? nbt_tx_lock(root_.legacy(), state_.app) : -1);
    if (!state_.updateLock.valid()) {
        state_.approved.wipe();
        return fail("application update is busy");
    }
    core::Status stopped;
    if (state_.op == Operation::HostStop) {
        state_.op = Operation::HostInstall;
    } else if (isRunning.get() && isRunning.value()) {
        stopped = beginHostStop(state_.app, Operation::HostInstall, nowMs);
    } else {
        stopped = beginStop(Operation::Install, nowMs);
    }
    if (!stopped.ok()) {
        releaseUpdateLock();
        state_.approved.wipe();
        return fail("cannot stop application");
    }
    state_.state = State::Installing;
    state_.installStartedMs = nowMs;
    snprintf(state_.message, sizeof(state_.message), "%s",
             state_.op == Operation::HostInstall ? "Stopping application" : "Installing update");
    return core::Status();
}

void Worker::installed() {
    if (state_.previouslyRunning && !hostStart().ok()) {
        fail("installed, but cannot start application");
        return;
    }
    if (!removeUpload().ok()) {
        fail("installed, but cannot remove upload");
        return;
    }
    state_.state = State::Installed;
    snprintf(state_.message, sizeof(state_.message), "%s",
             state_.settingsFailed ? "Update installed; settings not updated" : "Update installed");
}

core::Result<bool> Worker::hostGone(uint64_t nowMs) {
    core::Result<pid_t> pid = hosts_.pid(root_, state_.hostApp);
    if (!pid.get()) {
        return core::Result<bool>::failure(pid.status());
    }
    const uint64_t waited = nowMs >= state_.hostStopMs ? nowMs - state_.hostStopMs : 0;
    if (!pid.value()) {
        return core::Result<bool>::success(false);
    }
    if (waited >= kHostGiveUpMs) {
        return core::Result<bool>::failure(core::Status::error(ETIMEDOUT, "host stop timed out"));
    }
    if (waited >= kHostKillMs && !state_.hostKilled) {
        core::Status status = hosts_.signal(root_, state_.hostApp, SIGKILL);
        if (!status.ok()) {
            return core::Result<bool>::failure(status);
        }
        state_.hostKilled = true;
    }
    return core::Result<bool>::success(true);
}

void Worker::continueOperation(uint64_t nowMs) {
    const Operation op = state_.op;
    core::Result<bool> pending = core::Result<bool>::success(false);
    if (op == Operation::HostStop || op == Operation::HostInstall) {
        pending = hostGone(nowMs);
    } else if (state_.child.running()) {
        pending = children_.reap(state_.child, nowMs);
    }
    if (pending.get() && pending.value()) {
        return;
    }
    state_.op = Operation::None;
    if (!pending.get()) {
        if (op == Operation::Install || op == Operation::HostInstall) {
            releaseUpdateLock();
            state_.approved.wipe();
        }
        if (op == Operation::Restore) {
            fail("cannot stop candidate for recovery");
        } else if (op == Operation::Confirmed) {
            fail("installed, but cannot restore stopped state");
        } else {
            fail("cannot stop application");
        }
        return;
    }
    switch (op) {
    case Operation::Stop:
    case Operation::HostStop:
        snprintf(state_.message, sizeof(state_.message), "Application stopped");
        break;
    case Operation::Install:
    case Operation::HostInstall:
        install(nowMs);
        break;
    case Operation::Restore:
        restoreFinish(nowMs);
        break;
    case Operation::Confirmed:
        installed();
        break;
    case Operation::Fault:
        fail("application stopped unexpectedly");
        break;
    default:
        break;
    }
}

void Worker::poll(uint64_t nowMs) {
    (void)finishTask(nowMs);
    expire(nowMs);
    if (checkLog_.active()) { // until the candidate is gone, after the check too
        if (state_.child.running()) {
            checkLog_.drain();
        } else {
            checkLog_.end();
        }
    }
    if (state_.op != Operation::None) {
        continueOperation(nowMs);
        return;
    }
    if (!state_.child.running()) {
        return;
    }
    core::Result<bool> runningResult = children_.poll(state_.child, nowMs);
    if (state_.state == State::Installing) {
        const bool alive = runningResult.get() && runningResult.value();
        if (!alive || nowMs < state_.installStartedMs || nowMs - state_.installStartedMs > 45000) {
            checkLog_.failed(state_.child, alive);
            restoreBegin(nowMs);
            return;
        }
        if (children_.healthy(state_.child, nowMs)) {
            char error[128];
            if (nbt_tx_confirm(root_.legacy(), state_.app, error, sizeof(error))) {
                checkLog_.failed(state_.child, false);
                restoreBegin(nowMs);
                return;
            }
            checkLog_.passed();
            (void)nbt_app_fault_clear(root_.legacy(), state_.app);
            state_.settingsFailed = nbt_app_settings_save_schema(root_.legacy(), state_.app,
                                                                 &state_.pending.settings) != 0;
            releaseUpdateLock();
            snprintf(state_.message, sizeof(state_.message), "%s",
                     state_.previouslyRunning ? "Restarting application" : "Stopping application");
            if (!beginStop(Operation::Confirmed, nowMs).ok()) {
                fail("installed, but cannot restore stopped state");
            }
        }
    } else if (!runningResult.get() || !runningResult.value()) {
        if (!state_.child.running()) {
            fail("application stopped unexpectedly");
        } else if (!beginStop(Operation::Fault, nowMs).ok()) {
            fail("cannot stop application");
        }
    }
}

void Worker::shutdown() {
    if (shutdown_) {
        return;
    }
    shutdown_ = true;
    job_.wait(); // a copy in progress ends; an update's is then recovered below
    (void)job_.done();
    (void)taskLease_.reset();
    clearTask(task_);
    setupReady_ = false;
    cancelAfterTask_ = Ended::None;
    if (state_.child.running()) {
        (void)children_.stop(state_.child);
    }
    checkLog_.end();
    state_.op = Operation::None;
    if (state_.updateLock.valid()) {
        state_.previouslyRunning = false;
        restoreFinish(state_.installStartedMs);
    }
    if (state_.child.running()) {
        (void)children_.stop(state_.child);
    }
    releaseUpdateLock();
    state_.approved.wipe();
    state_.session.end();
    watchEnd();
}

} // namespace worker
} // namespace nbt
