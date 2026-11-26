// SPDX-License-Identifier: MPL-2.0
/* The worker's slow steps on the WorkerJob thread (worker_job.hpp): each
 * starts here with its TaskContext filled, and ends in finishTask() on the
 * loop, which logs how long it took and continues the step it belongs to.
 * busy() keeps everything that would touch the same files out meanwhile. */
#include "nbt/worker/worker.hpp"

#include "nbt/log.h"
#include "nbt/worker/usb_files.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {

bool Worker::initializeJobs() {
    return job_.initialize();
}

bool Worker::busy() const {
    return state_.state == State::Installing || job_.running() || setupReady_ || selfTestReady_;
}

void Worker::sets(fd_set *readers, int *highest) const {
    job_.sets(readers, highest);
}

void Worker::startTask(uint64_t nowMs) {
    task_.fs = root_.legacy();
    if (job_.start(runTask, &task_)) {
        return;
    }
    // Without a thread the step still runs, on the loop as before.
    nbt_logf("%s %s: no thread (errno %d); on the loop", taskName(task_.task), task_.app, errno);
    runTask(&task_);
    (void)finishTask(nowMs);
}

bool Worker::finishTask(uint64_t nowMs) {
    if (job_.running() && !job_.done()) {
        return false;
    }
    const Task task = task_.task;
    if (task == Task::None) {
        return false;
    }
    task_.task = Task::None;
    nbt_logf("%s %s: %llu ms%s", taskName(task), task_.app[0] ? task_.app : "manager",
             static_cast<unsigned long long>(task_.elapsedMs),
             task_.result < 0   ? " (failed)"
             : task_.result > 0 ? " (on trial)" // SETUP while an update is on trial
                                : "");
    switch (task) {
    case Task::VerifyNew:
    case Task::VerifyInstalled:
        verified();
        if (cancelAfterTask_ != Ended::None) {
            const Ended ended = cancelAfterTask_;
            cancelAfterTask_ = Ended::None;
            clearTask(task_);
            cancel(nowMs, ended);
            return true;
        }
        break;
    case Task::InstallNew:
        installNewDone();
        break;
    case Task::InstallUpdate:
        installCopied(nowMs);
        break;
    case Task::Setup:
        setupReady_ = true; // until takeSetup()
        return true;
    case Task::SelfTest:
        selfTestReady_ = true; // until takeSelfTest()
        return true;
    case Task::FilesScan:
    case Task::FilesImport:
        task_.usb->end(task_.result);
        break;
    default:
        break;
    }
    clearTask(task_);
    return true;
}

bool Worker::startSelfTest() {
    if (busy() || taskPending()) {
        return false;
    }
    clearTask(task_);
    task_.task = Task::SelfTest;
    startTask(0);
    return true;
}

bool Worker::takeSelfTest(int *result, SelfTestPrepared *prepared) {
    (void)finishTask(0);
    if (!selfTestReady_) {
        return false;
    }
    selfTestReady_ = false;
    *result = task_.result;
    *prepared = task_.selfTest;
    clearTask(task_);
    return true;
}

bool Worker::startFiles(UsbFiles &usb, bool importing, uint64_t nowMs) {
    if (busy() || taskPending()) {
        return false;
    }
    clearTask(task_);
    task_.task = importing ? Task::FilesImport : Task::FilesScan;
    task_.usb = &usb;
    snprintf(task_.app, sizeof(task_.app), "%s", usb.app());
    usb.begin(importing);
    startTask(nowMs);
    return true;
}

bool Worker::startSetup(const char *hmiRoot) {
    if (busy() || !hmiRoot) {
        return false;
    }
    clearTask(task_);
    task_.task = Task::Setup;
    task_.hmiRoot = hmiRoot;
    startTask(0);
    return true;
}

bool Worker::takeSetup(int *result, unsigned *changed, char *error, size_t size) {
    (void)finishTask(0); // the loop's poll may not have collected it yet
    if (!setupReady_) {
        return false;
    }
    setupReady_ = false;
    *result = task_.result;
    *changed = task_.changed;
    snprintf(error, size, "%s", task_.error);
    clearTask(task_);
    return true;
}

} // namespace worker
} // namespace nbt
