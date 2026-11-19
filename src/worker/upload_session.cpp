// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/worker.hpp"

#include "nbt/apps_key.h"
#include "nbt/bridge.h"
#include "nbt/fs.h"
#include "nbt/package/package_verifier.hpp"
#include "nbt/transaction.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

core::Status failure(const char *message) {
    return core::Status::error(errno ? errno : EIO, message);
}

} // namespace

core::Status Worker::removeSessionUpload() {
    const int exists = nbt_fs_exists(root_.legacy(), NBT_FS_SESSION_UPLOAD, "manager", NULL);
    if (exists < 0) {
        return failure("cannot inspect session upload");
    }
    if (exists && nbt_fs_unlink(root_.legacy(), NBT_FS_SESSION_UPLOAD, "manager", NULL)) {
        return failure("cannot remove session upload");
    }
    return core::Status();
}

core::Status Worker::removeUpload() {
    if (state_.generic || state_.newApp || state_.managerUpdate) {
        return removeSessionUpload();
    }
    if (!state_.app[0]) {
        return core::Status();
    }
    const int exists = nbt_fs_exists(root_.legacy(), NBT_FS_UPLOAD, state_.app, NULL);
    if (exists < 0) {
        return failure("cannot inspect upload");
    }
    if (exists && nbt_fs_unlink(root_.legacy(), NBT_FS_UPLOAD, state_.app, NULL)) {
        return failure("cannot remove upload");
    }
    return core::Status();
}

void Worker::watchEnd() {
    state_.ended = Ended::None;
}
void Worker::sessionEnd(uint64_t nowMs, Ended ended) {
    (void)nowMs;
    state_.ended = ended;
    state_.session.end();
}

core::Status Worker::openSession(const char *app, uint64_t nowMs) {
    if (busy()) {
        return core::Status::error(EBUSY, "installation in progress");
    }
    watchEnd();
    if (state_.session.active(0) || state_.app[0] || state_.generic) {
        state_.session.end();
        if (!removeUpload().ok()) {
            return fail("cannot clear previous upload");
        }
    }
    state_.generic = false;
    state_.newApp = false;
    state_.managerUpdate = false;
    if (!app) {
        if (!removeSessionUpload().ok() || !state_.session.start(nowMs).ok()) {
            return fail("cannot start session");
        }
        state_.generic = true;
        if (!state_.child.running()) {
            state_.app[0] = 0; // the package names the application; any is welcome
        }
    } else if (!selectApp(app).ok() || !removeUpload().ok() || !state_.session.start(nowMs).ok()) {
        return fail("cannot start session");
    }
    memset(&state_.pending, 0, sizeof(state_.pending));
    state_.state = State::Session;
    snprintf(state_.message, sizeof(state_.message), "Ready for upload");
    return core::Status();
}

void Worker::pageOpened() {
    if (state_.state == State::Session && state_.session.active(0)) {
        state_.session.setOpened(true);
    }
}

core::Result<core::UniqueFd> Worker::uploadBegin(uint64_t nowMs) {
    if (state_.state != State::Session || !state_.session.active(nowMs) ||
        state_.session.uploaded()) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::error(EPERM, "upload unavailable"));
    }
    core::Result<core::UniqueFd> upload =
        root_.open(state_.generic ? NBT_FS_SESSION_UPLOAD : NBT_FS_UPLOAD,
                   state_.generic ? "manager" : state_.app, NULL, NBT_FS_CREATE);
    if (!upload.get()) {
        fail("cannot create upload");
        return core::Result<core::UniqueFd>::failure(upload.status());
    }
    state_.session.setUploaded(true);
    return upload;
}

void Worker::uploadAbort() {
    if (!removeUpload().ok()) {
        fail("cannot remove incomplete upload");
    } else {
        fail("upload incomplete");
    }
}

core::Status Worker::reject(const char *reason) {
    if (!removeUpload().ok()) {
        return fail("cannot remove rejected upload");
    }
    snprintf(state_.message, sizeof(state_.message), "Package rejected: %s", reason);
    state_.state = State::Failed;
    memset(&state_.pending, 0, sizeof(state_.pending));
    return core::Status::error(EINVAL, "package rejected");
}

core::Status Worker::bindUpload() {
    if (uploadIsManagerRelease()) {
        return bindManager();
    }
    core::Result<core::UniqueFd> upload =
        root_.open(NBT_FS_SESSION_UPLOAD, "manager", NULL, NBT_FS_READ);
    if (!upload.get()) {
        return reject("upload missing");
    }
    core::Result<nbt_package> named = package::PackageVerifier::peekApp(upload.value().get());
    (void)upload.value().reset();
    if (!named.get()) {
        return reject("not an NBT Manager package");
    }
    const char *app = named.value().app;
    const int installed = nbt_fs_exists(root_.legacy(), NBT_FS_PROFILE, app, NULL);
    if (!strcmp(app, "manager") || installed < 0) {
        return reject("not an application package");
    }
    if (!installed) {
        return bindNewApp(app);
    }
    if (!selectApp(app).ok()) {
        char message[sizeof(state_.message)];
        memcpy(message, state_.message, sizeof(message));
        if (!removeSessionUpload().ok()) {
            return fail("cannot remove rejected upload");
        }
        state_.generic = false;
        snprintf(state_.message, sizeof(state_.message), "Package rejected: %.100s", message);
        return core::Status::error(EBUSY, "application unavailable");
    }
    state_.generic = false;
    if (!removeUpload().ok() ||
        nbt_fs_rename(root_.legacy(), NBT_FS_SESSION_UPLOAD, NBT_FS_UPLOAD, state_.app, NULL)) {
        state_.generic = true;
        return reject("cannot stage upload");
    }
    return core::Status();
}

// A package for an application not installed here: a new application, checked
// against the apps key and its own compatibility id (on the job's thread,
// verified()), installed from scratch once approved (installNew). Its upload
// stays the session's.
core::Status Worker::bindNewApp(const char *app) {
    core::Result<core::UniqueFd> upload =
        root_.open(NBT_FS_SESSION_UPLOAD, "manager", NULL, NBT_FS_READ);
    core::UniqueFd key(nbt_apps_key_fd());
    if (!upload.get() || !key.valid()) {
        return reject(upload.get() ? "no apps key in this manager" : "upload missing");
    }
    core::Result<nbt_package> named = package::PackageVerifier::peekApp(upload.value().get());
    if (!named.get()) {
        return reject("not an NBT Manager package");
    }
    clearTask(task_);
    task_.task = Task::VerifyNew;
    snprintf(task_.app, sizeof(task_.app), "%s", app);
    snprintf(task_.compatibility, sizeof(task_.compatibility), "%s", named.value().compatibility);
    task_.fd = upload.value().release();
    task_.keyFd = key.release();
    memcpy(state_.app, app, strlen(app) + 1);
    state_.generic = false;
    state_.newApp = true;
    return core::Status();
}

/* The upload is bound to its application here; its verification runs on the
 * job's thread and ends in verified(): STATUS says "Checking the package"
 * meanwhile. A manager release was checked when bound. */
core::Status Worker::uploadEnd(uint64_t nowMs) {
    if (state_.state != State::Session || !state_.session.uploaded() ||
        !state_.session.active(nowMs)) {
        uploadAbort();
        return core::Status::error(EPERM, "upload unavailable");
    }
    if (state_.generic && !bindUpload().ok()) {
        return core::Status::error(EINVAL, "upload binding failed");
    }
    if (state_.managerUpdate) {
        state_.state = State::Verified;
        snprintf(state_.message, sizeof(state_.message), "Package verified; approve in iDrive");
        return core::Status();
    }
    if (!state_.newApp) {
        clearTask(task_);
        task_.task = Task::VerifyInstalled;
        snprintf(task_.app, sizeof(task_.app), "%s", state_.app);
    }
    snprintf(state_.message, sizeof(state_.message), "Checking the package");
    startTask(nowMs);
    return core::Status();
}

void Worker::verified() {
    if (task_.result) {
        const bool fresh = state_.newApp;
        (void)reject(task_.error[0] ? task_.error : "verification failed");
        if (fresh) {
            state_.newApp = false;
            state_.app[0] = 0;
        }
        return;
    }
    state_.pending = task_.pending;
    // Before the car's review: the install would refuse it as "release
    // sequence rejected" (dev71 in the car: the same test app sent again).
    nbt_installed installed;
    if (!state_.newApp && !nbt_tx_installed(root_.legacy(), state_.app, &installed) &&
        state_.pending.sequence <= installed.highwater) {
        char reason[2 * NBT_VERSION_MAX + 48];
        snprintf(reason, sizeof(reason), "version %s is not newer than the installed %s",
                 state_.pending.version, installed.version);
        (void)reject(reason);
        return;
    }
    state_.state = State::Verified;
    snprintf(state_.message, sizeof(state_.message), "Package verified; approve in iDrive");
}

void Worker::cancel(uint64_t nowMs, Ended ended) {
    if (state_.state == State::Session && taskPending()) {
        cancelAfterTask_ = ended; // once the verification ends (finishTask)
        return;
    }
    if (busy()) {
        return;
    }
    sessionEnd(nowMs, ended);
    if (!removeUpload().ok()) {
        fail("cannot remove upload");
        return;
    }
    state_.generic = false;
    state_.newApp = false;
    state_.managerUpdate = false;
    memset(&state_.pending, 0, sizeof(state_.pending));
    state_.state = State::Idle;
    snprintf(state_.message, sizeof(state_.message), "%s",
             ended == Ended::Expired ? "Session expired" : "Session closed");
}

void Worker::cancel(uint64_t nowMs) {
    cancel(nowMs, Ended::Cancelled);
}

void Worker::expire(uint64_t nowMs) {
    if (busy()) {
        return;
    }
    const bool started = state_.session.started();
    if ((started && nowMs >= state_.session.expiresMs()) ||
        (!started && (state_.state == State::Session || state_.state == State::Verified))) {
        cancel(nowMs, Ended::Expired);
    }
}

} // namespace worker
} // namespace nbt
