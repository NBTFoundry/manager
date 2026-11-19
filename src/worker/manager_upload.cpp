// SPDX-License-Identifier: MPL-2.0
/* A manager release sent by a paired phone (PROTOCOL.md, "Manager updates
 * from a phone"). The worker cannot stage it: staging needs the GUI and the
 * operation locks that the launcher holds while the manager runs, and an
 * installed launcher that restarts a crashed child would run a slot staged
 * under it without a trial. So the worker verifies the release against the
 * manager key installed here, the driver reviews it on the car, and on
 * approval it becomes state/manager/release.upload with a stage request beside
 * it. The launcher stages it at the next opening (stage_request.cpp) and runs
 * it on trial; once confirmed, SETUP brings the launcher and plugin to it. */
#include "nbt/worker/worker.hpp"

#include "nbt/core/system.hpp"
#include "nbt/core/text.hpp"
#include "nbt/fs.h"
#include "nbt/launcher/slot_verifier.hpp"
#include "nbt/log.h"
#include "nbt/manager_refresh.h"
#include "nbt/manager_store.h"
#include "nbt/release.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

const char kReleaseV1Magic[] = "NBTRLS1\n";
const char kReleaseV2Magic[] = "NBTRLS2\n";

/* The operation lock: free, or held by the launcher that started this worker. */
int operationLock(nbt_fs *fs) {
    const int lock = nbt_fs_manager_operation_lock(fs);
    return lock >= 0 ? lock : nbt_fs_manager_operation_parent(fs);
}

/* Verifies `fd` as a manager release for this car: its manager key and
 * compatibility. 0 or -1 with `reason` set. */
int verifyRelease(nbt_fs *fs, int fd, nbt_release &release, const char *&reason) {
    char compat[49];
    if (nbt::launcher::SlotVerifier::readProfileCompat(fs, compat)) {
        reason = "invalid manager profile";
        return -1;
    }
    const int key = nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ);
    const int result = key < 0 ? -1 : nbt_release_verify(fd, key, compat, &release, NULL, 0);
    if (key >= 0) {
        close(key);
    }
    if (result) {
        reason = "not an NBT Manager release for this car";
    }
    return result;
}

} // namespace

bool Worker::uploadIsManagerRelease() {
    char head[8];
    core::Result<core::UniqueFd> upload =
        root_.open(NBT_FS_SESSION_UPLOAD, "manager", NULL, NBT_FS_READ);
    return upload.get() &&
           pread(upload.value().get(), head, sizeof(head), 0) ==
               static_cast<ssize_t>(sizeof(head)) &&
           (!memcmp(head, kReleaseV1Magic, sizeof(head)) ||
            !memcmp(head, kReleaseV2Magic, sizeof(head)));
}

core::Status Worker::bindManager() {
    nbt_release release;
    nbt_manager_state state;
    const char *reason = NULL;
    core::Result<core::UniqueFd> upload =
        root_.open(NBT_FS_SESSION_UPLOAD, "manager", NULL, NBT_FS_READ);
    if (!upload.get()) {
        return reject("upload missing");
    }
    if (verifyRelease(root_.legacy(), upload.value().get(), release, reason)) {
        return reject(reason);
    }
    if (nbt_manager_store_inspect(root_.legacy(), &state)) {
        return reject("NBT Manager's state cannot be read");
    }
    if (state.pending) {
        return reject("an NBT Manager update is already waiting: open App Manager again");
    }
    if (release.sequence <= state.highwater) {
        return reject("this NBT Manager is not newer than the installed one");
    }
    const int current = nbt_manager_launcher_current(root_.legacy(), state_.managerInstalled);
    if (current <= 0) {
        return reject(current < 0 ? "the installed NBT Manager failed verification"
                                  : "update NBT Manager once with the installer first");
    }
    if (release.format_version >= 2 &&
        nbt_manager_launcher_supports_format(root_.legacy(), "NBTRLS2") != 1) {
        return reject("install and confirm the NBTRLS1 compatibility release first");
    }
    memset(&state_.pending, 0, sizeof(state_.pending));
    snprintf(state_.pending.app, sizeof(state_.pending.app), "manager");
    snprintf(state_.pending.version, sizeof(state_.pending.version), "%s", release.version);
    state_.pending.sequence = release.sequence;
    memcpy(state_.pending.package_hash, release.sha256, sizeof(release.sha256));
    snprintf(state_.app, sizeof(state_.app), "manager");
    state_.generic = false;
    state_.newApp = false;
    state_.managerUpdate = true;
    return core::Status();
}

core::Status Worker::approveManager(const unsigned char digest[32], uint64_t nowMs) {
    nbt_release release;
    const char *reason = NULL;
    char hex[65];
    char request[96];
    if (state_.state != State::Verified || state_.op != Operation::None ||
        !state_.session.active(nowMs) || !digest ||
        !core::constantEqual(core::Span<const uint8_t>(digest, 32),
                             core::Span<const uint8_t>(state_.pending.package_hash, 32))) {
        return core::Status::error(EACCES, "approval rejected");
    }
    nbt_fs *fs = root_.legacy();
    core::UniqueFd lock(operationLock(fs));
    if (!lock.valid()) {
        return fail("NBT Manager is busy");
    }
    {
        core::Result<core::UniqueFd> upload =
            root_.open(NBT_FS_SESSION_UPLOAD, "manager", NULL, NBT_FS_READ);
        if (!upload.get() || verifyRelease(fs, upload.value().get(), release, reason) ||
            !nbt_constant_equal(release.sha256, digest, 32)) {
            return fail("the update changed before it was approved");
        }
    }
    if (!core::hexEncode(core::Span<const uint8_t>(digest, 32), core::Span<char>(hex, sizeof(hex)))
             .ok() ||
        snprintf(request, sizeof(request), "NBTSTAGE1\nsha256=%s\n", hex) >=
            static_cast<int>(sizeof(request))) {
        return fail("cannot record the update");
    }
    const int previous = nbt_fs_exists(fs, NBT_FS_MANAGER_STAGE_REQUEST, "manager", NULL);
    const int upload = nbt_fs_exists(fs, NBT_FS_MANAGER_UPLOAD, "manager", NULL);
    if (previous < 0 || upload < 0 ||
        (previous && nbt_fs_unlink(fs, NBT_FS_MANAGER_STAGE_REQUEST, "manager", NULL)) ||
        (upload && nbt_fs_unlink(fs, NBT_FS_MANAGER_UPLOAD, "manager", NULL)) ||
        nbt_fs_rename(fs, NBT_FS_SESSION_UPLOAD, NBT_FS_MANAGER_UPLOAD, "manager", NULL)) {
        return fail("cannot keep the update");
    }
    core::UniqueFd out(
        nbt_fs_open(fs, NBT_FS_MANAGER_STAGE_REQUEST, "manager", NULL, NBT_FS_CREATE));
    const size_t length = strlen(request);
    if (!out.valid() || write(out.get(), request, length) != static_cast<ssize_t>(length) ||
        fsync(out.get())) {
        (void)nbt_fs_unlink(fs, NBT_FS_MANAGER_STAGE_REQUEST, "manager", NULL);
        return fail("cannot record the update");
    }
    nbt_logf("manager %s approved; staged at the next opening", release.version);
    sessionEnd(nowMs, Ended::None);
    state_.managerUpdate = false;
    state_.state = State::Installed;
    snprintf(state_.message, sizeof(state_.message),
             "NBT Manager %s starts when App Manager opens again", release.version);
    return core::Status();
}

} // namespace worker
} // namespace nbt
