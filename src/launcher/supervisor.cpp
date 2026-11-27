// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/supervisor.hpp"

#include "nbt/display/layer_manager.hpp"
#include "nbt/launcher/manager_crash.hpp"
#include "nbt/launcher/manager_worker.hpp"
#include "nbt/launcher/slot_select.hpp"
#include "nbt/launcher/slot_store.hpp"
#include "nbt/launcher/update_handover.hpp"
#include "nbt/log.h"
#include "nbt/manager_stage.h"
#include "nbt/manager_supervisor.h"
#include "nbt/process/child_process.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace launcher {
namespace {

core::Result<core::UniqueFd> waitGuiLock(const storage::ManagedRoot &root) {
    core::Result<core::UniqueFd> opened =
        root.open(NBT_FS_MANAGER_LOCK, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (!opened.ok()) {
        return core::Result<core::UniqueFd>::failure(opened.status());
    }
    core::UniqueFd fd(opened.value().release());
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    const struct timespec pauseTime = {0, 100000000};
    for (unsigned i = 0; i < 100; ++i) {
        if (!fcntl(fd.get(), F_SETLK, &lock)) {
            return core::Result<core::UniqueFd>::success(std::move(fd));
        }
        if (errno != EACCES && errno != EAGAIN && errno != EINTR) {
            break;
        }
        nanosleep(&pauseTime, NULL);
    }
    return core::Result<core::UniqueFd>::failure(
        core::Status::fromErrno("manager GUI lock failed"));
}

core::Status restart(SlotStore &store, bool trial, const char *reason) {
    const time_t now = time(NULL);
    if (now == static_cast<time_t>(-1)) {
        return core::Status::fromErrno("restart clock failed");
    }
    core::Result<unsigned> delay = store.recordRestart(static_cast<uint64_t>(now));
    if (!delay.ok()) {
        // The failure is logged even when no restart follows (dev26 showed only the limit).
        nbt_logf("%s; no restart: %s", reason, delay.status().message());
        if (delay.status().code() == ELOOP && trial) {
            nbt_logf("restart limit reached; restoring known-good slot");
            const core::Status rolledBack = store.rollback();
            if (!rolledBack.ok()) {
                return rolledBack;
            }
        }
        return delay.status();
    }
    nbt_logf("%s; restart in %u ms", reason, delay.value());
    struct timespec pauseTime = {static_cast<time_t>(delay.value() / 1000),
                                 static_cast<long>(delay.value() % 1000) * 1000000L};
    while (nanosleep(&pauseTime, &pauseTime) && errno == EINTR) {
    }
    return core::Status();
}

core::Status failure(const char *message) {
    return core::Status::error(errno ? errno : EIO, message);
}

enum class Ended { Failed, Closed, Update };

/* Watches the GUI and the worker until either ends. Closed: the user closed
 * the GUI; Update: it ended for an approved manager update; Failed: `reason`
 * says what failed and the GUI is stopped. */
core::Status watch(process::ChildProcess &gui, ManagerWorker &worker, SlotStore &store,
                   nbt_manager_state &state, bool &trial, UpdateHandover &handover, Ended &ended,
                   char *reason, size_t size) {
    bool readyLogged = false;
    ended = Ended::Failed;
    const struct timespec pauseTime = {0, 100000000};
    for (;;) {
        uint64_t now;
        if (nbt_monotonic_ms(&now)) {
            return failure("monotonic clock failed");
        }
        core::Result<bool> running = gui.poll(now);
        if (!running.ok()) {
            snprintf(reason, size, "%s",
                     gui.ready() ? "manager GUI heartbeat or lease lost"
                                 : "manager GUI not ready within 5 s");
            return gui.stop();
        }
        if (!running.value()) {
            if (!trial && gui.exitStatus() == 0 && !gui.exitSignal()) {
                nbt_logf("GUI closed");
                ended = Ended::Closed;
                return core::Status();
            }
            if (!trial && gui.exitStatus() == NBT_MANAGER_GUI_EXIT_UPDATE && !gui.exitSignal()) {
                handover.startBeats(); // first: the plugin's next look must find the manager
                nbt_logf("GUI closed for the manager update");
                ended = Ended::Update;
                return core::Status();
            }
            if (gui.exitSignal()) {
                snprintf(reason, size, "manager GUI terminated by signal %d", gui.exitSignal());
            } else {
                snprintf(reason, size, "manager GUI exited with status %d", gui.exitStatus());
            }
            return core::Status();
        }
        if (!worker.running()) {
            snprintf(reason, size, "manager worker %s", worker.exitReason());
            return gui.stop();
        }
        if (!readyLogged && gui.ready()) {
            readyLogged = true;
            // The GUI holds byte 0 of gui.lock by now; byte 1 lets the launcher
            // keep iDrive held should this GUI end for a manager update.
            if (trial) {
                handover.release();
            } else {
                (void)handover.arm();
            }
            nbt_logf(
                "GUI ready after %llu ms",
                static_cast<unsigned long long>(gui.legacy().ready_ms - gui.legacy().started_ms));
        }
        if (trial && gui.healthy(now)) {
            core::Status status = store.confirm(gui.legacy(), now);
            if (!status.ok()) {
                return status;
            }
            trial = false;
            if (!store.load(state).ok()) {
                return failure("manager state reload failed");
            }
            (void)handover.arm(); // confirmed: as a GUI started off trial
        }
        if (!trial && state.restarts && gui.ready() && now >= gui.legacy().ready_ms &&
            now - gui.legacy().ready_ms >= 60000) {
            const core::Status status = store.clearRestarts();
            if (!status.ok()) {
                nbt_logf("restart count kept: %s (errno %d)", status.message(), status.code());
            }
            state.restarts = 0;
            state.restart_epoch = 0;
        }
        nanosleep(&pauseTime, NULL);
    }
}

/* The manager operation lock again on its own descriptor: staging opened and
 * closed the file, which drops this process's lock (POSIX). */
core::Status relockOperation(const core::UniqueFd &operation) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    return fcntl(operation.get(), F_SETLK, &lock)
               ? core::Status::fromErrno("manager operation lock lost while staging")
               : core::Status();
}

} // namespace

core::Status Supervisor::run(storage::ManagedRoot &root) {
    if (!root.valid()) {
        return core::Status::error(EINVAL, "manager root is not initialized");
    }
    core::Result<core::UniqueFd> operationResult = root.managerOperationLock();
    if (!operationResult.ok()) {
        return operationResult.status();
    }
    core::UniqueFd operation(operationResult.value().release());
    core::Result<core::UniqueFd> guardResult = waitGuiLock(root);
    if (!guardResult.ok()) {
        return guardResult.status();
    }
    core::UniqueFd guiGuard(guardResult.value().release());

    SlotStore store(root);
    nbt_manager_state state;
    bool trial = false;
    core::Status status = selectSlot(root, store, state, trial);
    if (!status.ok()) {
        return status;
    }
    status = guiGuard.close();
    if (!status.ok()) {
        return status;
    }

    UpdateHandover handover(root);
    bool handoverStart = false; // the next GUI follows one that ended for an update
    for (;;) {
        uint64_t now;
        if (nbt_monotonic_ms(&now)) {
            return failure("monotonic clock failed");
        }
        char reason[96];
        reason[0] = 0;
        Ended ended = Ended::Failed;
        ManagerWorker worker;
        core::Result<core::UniqueFd> channel = worker.start(root, state.active.slot);
        if (!channel.ok()) {
            snprintf(reason, sizeof(reason), "manager worker start failed (%s)",
                     strerror(channel.status().code()));
            if (handoverStart) { // no GUI follows now: let iDrive go
                handover.abandon();
                handoverStart = false;
            }
        } else {
            if (handoverStart) {
                handover.stopBeats(); // the new GUI writes its own line first thing
                (void)setenv(NBT_MANAGER_HANDOVER_ENV, "1", 1);
            }
            core::Result<process::ChildProcess> started = process::ChildProcess::startManager(
                root, state.active.slot, channel.value().get(), now);
            (void)unsetenv(NBT_MANAGER_HANDOVER_ENV);
            handoverStart = false;
            channel.value().reset();
            if (!started.ok()) {
                snprintf(reason, sizeof(reason), "manager GUI start failed (%s)",
                         strerror(started.status().code()));
            } else {
                process::ChildProcess gui = std::move(started.value());
                nbt_logf("GUI started, pid %d", static_cast<int>(gui.pid()));
                status = watch(gui, worker, store, state, trial, handover, ended, reason,
                               sizeof(reason));
                // However the GUI ended, iDrive gets the panel back, from an app too.
                if (display::restore()) {
                    nbt_logf("GUI left a surface on the panel; iDrive restored");
                }
                if (!status.ok()) {
                    return status;
                }
            }
        }
        worker.stop(); // the GUI is gone: the worker sees its channel close
        if (ended == Ended::Update) {
            // The approved release now, as the next opening would; iDrive stays
            // held meanwhile, and the new GUI shows itself at once.
            (void)nbt_manager_stage_requested(root.legacy());
            status = relockOperation(operation);
            if (!status.ok()) {
                return status;
            }
            (void)handover.rearm();
            status = selectSlot(root, store, state, trial);
            if (!status.ok()) {
                return status;
            }
            handoverStart = handover.continuous();
            if (!handoverStart) {
                nbt_logf("update handover: iDrive released (%s); the driver opens the new release",
                         handover.broken());
                handover.abandon();
            }
            continue;
        }
        handover.release();
        if (ended == Ended::Closed) {
            nbt_logf("manager closed");
            return state.restarts ? store.clearRestarts() : core::Status();
        }
        reportManagerCrash(root, state, reason);
        status = restart(store, trial, reason);
        if (!status.ok()) {
            return status;
        }
        if (!store.load(state).ok()) {
            return failure("manager state reload failed");
        }
    }
}

} // namespace launcher
} // namespace nbt

int nbt_manager_supervise(nbt_fs *fs) {
    if (!fs) {
        return -1;
    }
    nbt::storage::ManagedRoot root(*fs);
    nbt::launcher::Supervisor supervisor;
    (void)nbt_log_open(fs, "launcher");
    nbt_logf("open");
    (void)nbt_manager_stage_requested(fs);
    const nbt::core::Status status = supervisor.run(root);
    if (!status.ok()) {
        nbt_logf("%s (%s)", status.message(), strerror(status.code()));
    }
    nbt_log_close();
    return status.ok() ? 0 : -1;
}
