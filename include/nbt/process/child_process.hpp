// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_CHILD_PROCESS_HPP
#define NBT_PROCESS_CHILD_PROCESS_HPP

#include "nbt/core/status.hpp"
#include "nbt/process.h"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace process {

class ChildProcess {
public:
    ChildProcess();
    ~ChildProcess();
    ChildProcess(ChildProcess &&other);
    ChildProcess &operator=(ChildProcess &&other);
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;

    /* `outputFd`, when not -1, becomes the application's stdout and stderr. */
    static core::Result<ChildProcess> start(const storage::ManagedRoot &root, const char *app,
                                            char *const *environment, int statusFd, int mostFd,
                                            int outputFd, uint64_t nowMs);
    static core::Result<ChildProcess> startManager(const storage::ManagedRoot &root, unsigned slot,
                                                   int workerFd, uint64_t nowMs);
    core::Result<bool> poll(uint64_t nowMs);
    bool healthy(uint64_t nowMs) const;
    core::Status stop();
    core::Status terminate(uint64_t nowMs);
    core::Result<bool> reap(uint64_t nowMs);
    bool running() const {
        return child_.pid > 0;
    }
    bool ready() const {
        return child_.ready != 0;
    }
    bool stopping() const {
        return child_.stopping != 0;
    }
    pid_t pid() const {
        return child_.pid;
    }
    int exitStatus() const {
        return child_.exit_status;
    }
    int exitSignal() const {
        return child_.exit_signal;
    }
    const nbt_process &legacy() const {
        return child_;
    }
#ifdef NBT_TESTING
    static ChildProcess forTesting(pid_t pid, uint64_t readyMs);
    void setStoppingForTesting(uint64_t nowMs);
    void resetForTesting();
#endif

private:
    nbt_process child_;
#ifdef NBT_TESTING
    bool testOnly_;
#endif
};

class ChildRunner {
public:
    virtual ~ChildRunner() {
    }
    /* An install health check's run; `outputFd` as in ChildProcess::start. */
    virtual core::Result<ChildProcess> start(const storage::ManagedRoot &root, const char *app,
                                             char *const *environment, int outputFd,
                                             uint64_t nowMs) = 0;
    virtual core::Result<bool> poll(ChildProcess &child, uint64_t nowMs) = 0;
    virtual bool healthy(const ChildProcess &child, uint64_t nowMs) = 0;
    virtual core::Status stop(ChildProcess &child) = 0;
    virtual core::Status terminate(ChildProcess &child, uint64_t nowMs) = 0;
    virtual core::Result<bool> reap(ChildProcess &child, uint64_t nowMs) = 0;
};

class RealChildRunner : public ChildRunner {
public:
    core::Result<ChildProcess> start(const storage::ManagedRoot &root, const char *app,
                                     char *const *environment, int outputFd,
                                     uint64_t nowMs) override;
    core::Result<bool> poll(ChildProcess &child, uint64_t nowMs) override;
    bool healthy(const ChildProcess &child, uint64_t nowMs) override;
    core::Status stop(ChildProcess &child) override;
    core::Status terminate(ChildProcess &child, uint64_t nowMs) override;
    core::Result<bool> reap(ChildProcess &child, uint64_t nowMs) override;
};

} // namespace process
} // namespace nbt

#endif
