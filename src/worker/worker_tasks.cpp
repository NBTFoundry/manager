#include "nbt/worker/worker_tasks.hpp"

#include "nbt/app_provision.h"
#include "nbt/app_remove.h"
#include "nbt/bridge.h"
#include "nbt/core/secure.hpp"
#include "nbt/manager_refresh.h"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

void closeOwned(int &fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

int verifyNew(TaskContext &c) {
    nbt_bridge_provider provider;
    nbt_bridge_current(&provider);
    return nbt_package_verify(c.fd, c.keyFd, c.app, c.compatibility, &c.pending, c.error,
                              sizeof(c.error)) ||
                   nbt_bridge_check(&c.pending.bridge, &provider, NULL, NULL, c.error,
                                    sizeof(c.error))
               ? -1
               : 0;
}

int installNew(TaskContext &c) {
    // What an uninstall or a first install cut short left of it goes first.
    if (nbt_fs_app_present(c.fs, c.app) == 1 && nbt_app_remove(c.fs, c.app) != NBT_APP_REMOVED) {
        snprintf(c.error, sizeof(c.error), "an earlier copy cannot be removed");
        return -1;
    }
    if (c.fd < 0) {
        snprintf(c.error, sizeof(c.error), "upload missing");
        return -1;
    }
    return nbt_app_install_new(c.fs, c.app, c.fd, c.digest, c.error, sizeof(c.error));
}

} // namespace

void clearTask(TaskContext &context) {
    core::secureClear(context.digest, sizeof(context.digest));
    memset(&context, 0, sizeof(context));
    context.fd = -1;
    context.keyFd = -1;
}

const char *taskName(Task task) {
    switch (task) {
    case Task::VerifyNew:
    case Task::VerifyInstalled:
        return "verification";
    case Task::InstallNew:
    case Task::InstallUpdate:
        return "install copy";
    case Task::Setup:
        return "setup";
    default:
        return "task";
    }
}

void runTask(void *raw) {
    TaskContext &c = *static_cast<TaskContext *>(raw);
    uint64_t started = 0, ended = 0;
    (void)nbt_monotonic_ms(&started);
    c.error[0] = 0;
#ifdef NBT_TESTING
    const char *delay = getenv("NBT_TEST_TASK_DELAY_MS"); // a slow car's flash
    if (delay && delay[0]) {
        const long ms = strtol(delay, NULL, 10);
        const struct timespec pause = {static_cast<time_t>(ms / 1000), (ms % 1000) * 1000000L};
        nanosleep(&pause, NULL);
    }
#endif
    switch (c.task) {
    case Task::VerifyNew:
        c.result = verifyNew(c);
        break;
    case Task::VerifyInstalled:
        c.result = nbt_tx_verify(c.fs, c.app, &c.pending, c.error, sizeof(c.error));
        break;
    case Task::InstallNew:
        c.result = installNew(c);
        break;
    case Task::InstallUpdate:
        c.result = nbt_tx_begin(c.fs, c.app, c.digest, c.recovery ? 1 : 0, &c.pending, c.error,
                                sizeof(c.error));
        break;
    case Task::Setup:
        c.result = nbt_manager_refresh_installed(c.fs, c.hmiRoot, nbt_system_remount, &c.changed,
                                                 c.error, sizeof(c.error));
        break;
    default:
        c.result = -1;
        break;
    }
    closeOwned(c.fd);
    closeOwned(c.keyFd);
    core::secureClear(c.digest, sizeof(c.digest));
    (void)nbt_monotonic_ms(&ended);
    c.elapsedMs = ended >= started ? ended - started : 0;
}

} // namespace worker
} // namespace nbt
