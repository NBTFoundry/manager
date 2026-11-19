// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_MANAGER_WORKER_HPP
#define NBT_LAUNCHER_MANAGER_WORKER_HPP

#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/storage/managed_root.hpp"

#include <sys/types.h>

namespace nbt {
namespace launcher {

/* The manager worker, started by the launcher before the GUI. The launcher
 * creates the private control channel (PROTOCOL.md): the worker gets one end
 * as its descriptor 3, the GUI the other. The worker ends when the GUI's end
 * closes; stop() waits for that and kills it only if it does not exit. */
class ManagerWorker {
public:
    ManagerWorker();
    ~ManagerWorker();
    ManagerWorker(const ManagerWorker &) = delete;
    ManagerWorker &operator=(const ManagerWorker &) = delete;

    /* Starts the slot's worker; returns the GUI's end of the channel. */
    core::Result<core::UniqueFd> start(const storage::ManagedRoot &root, unsigned slot);
    /* True while the worker runs; false once it has exited (and been reaped). */
    bool running();
    /* Waits for the worker to exit after the GUI's end closed, then kills it. */
    void stop();
    pid_t pid() const {
        return pid_;
    }
    /* "exited with status N" or "terminated by signal N", after running() is false. */
    const char *exitReason() const {
        return exitReason_;
    }

private:
    void reaped(int status);

    pid_t pid_;
    char exitReason_[48];
};

} // namespace launcher
} // namespace nbt

#endif
