// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_WORKER_TASKS_HPP
#define NBT_WORKER_WORKER_TASKS_HPP

/* The slow steps the worker runs on its WorkerJob thread. Each reads and
 * writes only its TaskContext (and the files under the locks the worker
 * holds for it); the loop reads the outcome once the job is done. */

#include "nbt/fs.h"
#include "nbt/package.h"
#include "nbt/worker/self_test.hpp"

namespace nbt {
namespace worker {

enum class Task {
    None,
    VerifyNew,
    VerifyInstalled,
    InstallNew,
    InstallUpdate,
    Setup,
    FilesScan,   // an application's files on a USB stick (usb_files.hpp)
    FilesImport, // copied to the car
    SelfTest     // the bundled applications installed (self_test.hpp)
};

class UsbFiles;

struct TaskContext {
    Task task;
    nbt_fs *fs;
    char app[NBT_ID_MAX + 1];
    int fd;    // the upload (VerifyNew, InstallNew); closed by the task
    int keyFd; // unused: VerifyNew finds the signer's key (nbt/signers.h)
    // The package's signer: found by VerifyNew, read from the profile by
    // VerifyInstalled, given to InstallNew ("" for the apps key).
    char signer[65];
    char compatibility[NBT_ID_MAX + 1];
    nbt_package pending;
    unsigned char digest[32];
    bool recovery;
    const char *hmiRoot;
    UsbFiles *usb; // FilesScan, FilesImport
    unsigned changed;
    SelfTestPrepared selfTest; // SelfTest
    uint64_t elapsedMs;
    int result;
    char error[128];
};

void clearTask(TaskContext &context);
/* The WorkerJob task: runs context->task, then sets result, error and
 * elapsedMs, and wipes the approved digest. */
void runTask(void *context);
const char *taskName(Task task);

} // namespace worker
} // namespace nbt

#endif
