// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/worker.hpp"

#include "nbt/lifecycle/app_settings.hpp"
#include "nbt/transaction.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <utility>

namespace nbt {
namespace worker {

// The copy (nbt_tx_begin) runs on the job's thread under the application's
// lease, then installCopied() starts the candidate's health check.
void Worker::install(uint64_t nowMs) {
    core::Result<core::UniqueFd> lease = root_.lease(state_.app);
    if (!lease.get()) {
        releaseUpdateLock();
        state_.approved.wipe();
        fail("application is still running");
        return;
    }
    taskLease_ = std::move(lease.value());
    clearTask(task_);
    task_.task = Task::InstallUpdate;
    snprintf(task_.app, sizeof(task_.app), "%s", state_.app);
    memcpy(task_.digest, state_.approved.data(), sizeof(task_.digest));
    task_.recovery = state_.recovery;
    task_.pending = state_.pending;
    state_.approved.wipe();
    snprintf(state_.message, sizeof(state_.message), "Installing update");
    startTask(nowMs);
}

void Worker::installCopied(uint64_t nowMs) {
    char error[128];
    if (task_.result) {
        const int recovered = nbt_tx_recover(root_.legacy(), state_.app, error, sizeof(error));
        (void)taskLease_.reset();
        if (recovered) {
            fail("installation failed; recovery required");
            return;
        }
        releaseUpdateLock();
        if (!restartAfterUpdate().ok()) {
            fail("installation failed; previous application cannot start");
            return;
        }
        fail("cannot install verified package");
        return;
    }
    state_.pending = task_.pending;
    (void)taskLease_.reset();
    state_.installStartedMs = nowMs;
    nbt_bridge_requirement bridge;
    core::Result<process::ChildProcess> child = core::Result<process::ChildProcess>::failure(
        core::Status::error(EIO, "candidate setup failed"));
    const int output = checkLog_.begin(root_.legacy(), state_.app);
    if (!nbt_tx_candidate_bridge(root_.legacy(), state_.app, &bridge)) {
        lifecycle::AppEnvironment environment;
        if (environment.build(root_, state_.app, "check", &bridge, NULL).ok()) {
            child = children_.start(root_, state_.app, environment.list(), output, nowMs);
        }
    }
    if (!child.get()) {
        checkLog_.notStarted(state_.pending.version, child.status().code());
        restoreBegin(nowMs);
        return;
    }
    state_.child = std::move(child.value());
    checkLog_.started(state_.pending.version, state_.pending.sequence, state_.child);
    snprintf(state_.message, sizeof(state_.message), "Checking application health");
}

} // namespace worker
} // namespace nbt
