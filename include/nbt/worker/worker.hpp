// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_WORKER_HPP
#define NBT_WORKER_WORKER_HPP

#include "nbt/core/secure.hpp"
#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/package.h"
#include "nbt/process/child_process.hpp"
#include "nbt/process/host_control.hpp"
#include "nbt/storage/managed_root.hpp"
#include "nbt/worker/install_check_log.hpp"
#include "nbt/worker/session.hpp"
#include "nbt/worker/worker_job.hpp"
#include "nbt/worker/worker_tasks.hpp"

namespace nbt {
namespace worker {

static const uint64_t kHostKillMs = 8000;
static const uint64_t kHostGiveUpMs = 15000;
static const uint64_t kWatchMs = UINT64_C(5) * 60 * 1000;

enum class State { Idle, Session, Verified, Installing, Installed, Failed };
enum class Operation { None, Stop, Install, Restore, Confirmed, Fault, HostStop, HostInstall };
enum class Ended { None, Cancelled, Expired };
/* Screen: started or shown, and its host has the application surface, so the
 * GUI hands it the panel. ScreenTaken: another application's host has it. */
enum class StartResult { Started, AlreadyRunning, Screen, ScreenTaken, Quarantined };

struct WorkerState {
    WorkerState();

    process::ChildProcess child;
    Session session;
    Ended ended;
    bool generic;
    bool newApp;                // the session's package installs an application not installed here
    bool managerUpdate = false; // the session's upload is a manager release (manager_upload.cpp)
    char managerInstalled[49] = {}; // the running manager's version, for STATUS
    nbt_package pending;
    char app[NBT_ID_MAX + 1];
    char hostApp[NBT_ID_MAX + 1];
    uint64_t hostStopMs;
    bool hostKilled;
    char message[128];
    uint64_t installStartedMs;
    core::UniqueFd updateLock;
    bool previouslyRunning;
    bool settingsFailed;
    bool recovery;
    core::SecureBytes<32> approved;
    Operation op;
    State state;
};

class Worker {
public:
    Worker(storage::ManagedRoot &root, process::ChildRunner &children, process::HostControl &hosts);
    ~Worker();
    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;

    storage::ManagedRoot &root() {
        return root_;
    }
    WorkerState &state() {
        return state_;
    }
    const WorkerState &state() const {
        return state_;
    }

    core::Status openSession(const char *app, uint64_t nowMs);
    void pageOpened();
    core::Result<core::UniqueFd> uploadBegin(uint64_t nowMs);
    core::Status uploadEnd(uint64_t nowMs);
    void uploadAbort();
    core::Result<StartResult> start(const char *app, uint64_t nowMs);
    core::Status stop(const char *app, uint64_t nowMs);
    /* Uninstalls a stopped application: an NBT_APP_REMOVE* result
     * (nbt/app_remove.h), BUSY while a session, install or stop is under way. */
    core::Result<int> remove(const char *app, uint64_t nowMs);
    core::Result<bool> running(const char *app);
    core::Status approve(const unsigned char digest[32], bool recovery, uint64_t nowMs);
    void cancel(uint64_t nowMs);
    void poll(uint64_t nowMs);
    void shutdown();

    /* The slow steps on their own thread (worker_jobs.cpp). */
    bool initializeJobs();
    /* Installing, or a verification, copy or SETUP on the job's thread. */
    bool busy() const;
    void sets(fd_set *readers, int *highest) const;
    /* SETUP on the job's thread; false when busy. Its outcome once, from
     * takeSetup: nbt_manager_refresh_installed's result and changes. */
    bool startSetup(const char *hmiRoot);
    bool takeSetup(int *result, unsigned *changed, char *error, size_t size);
    /* A USB scan or import on the job's thread (usb_files.hpp); false when
     * busy. Its end sets `usb`'s phase. */
    bool startFiles(UsbFiles &usb, bool importing, uint64_t nowMs);
    /* A step handed to the job and not yet collected; collectTask() collects
     * an ended one (poll() does too, with the rest of the worker's work). */
    bool taskPending() const {
        return task_.task != Task::None;
    }
    bool collectTask(uint64_t nowMs) {
        return finishTask(nowMs);
    }

private:
    core::Status fail(const char *message);
    core::Status selectApp(const char *app);
    core::Status removeSessionUpload();
    core::Status removeUpload();
    void watchEnd();
    void sessionEnd(uint64_t nowMs, Ended ended);
    core::Status reject(const char *reason);
    core::Status bindUpload();
    core::Status bindNewApp(const char *app);
    core::Status installNew(const unsigned char digest[32], uint64_t nowMs);
    bool uploadIsManagerRelease();
    core::Status bindManager();
    core::Status approveManager(const unsigned char digest[32], uint64_t nowMs);
    core::Status approveUpdate(const unsigned char digest[32], bool recovery, uint64_t nowMs);
    core::Status beginStop(Operation op, uint64_t nowMs);
    void releaseUpdateLock();
    core::Status beginHostStop(const char *app, Operation op, uint64_t nowMs);
    core::Status hostStart();
    void restoreFinish(uint64_t nowMs);
    void restoreBegin(uint64_t nowMs);
    void install(uint64_t nowMs);
    void cancel(uint64_t nowMs, Ended ended);
    void expire(uint64_t nowMs);
    void installed();
    core::Result<bool> hostGone(uint64_t nowMs);
    void continueOperation(uint64_t nowMs);
    void startTask(uint64_t nowMs);
    bool finishTask(uint64_t nowMs);
    void verified();
    void installNewDone();
    void installCopied(uint64_t nowMs);

    storage::ManagedRoot &root_;
    process::ChildRunner &children_;
    process::HostControl &hosts_;
    WorkerState state_;
    InstallCheckLog checkLog_;
    bool shutdown_;
    WorkerJob job_;
    TaskContext task_;
    core::UniqueFd taskLease_; // the application's lease while its update copies
    bool setupReady_ = false;
    Ended cancelAfterTask_ = Ended::None; // a CANCEL during a verification
};

} // namespace worker
} // namespace nbt

#endif
