// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_INSTALL_CHECK_LOG_HPP
#define NBT_WORKER_INSTALL_CHECK_LOG_HPP

/* The install health check's run of a new version, in the application's own
 * log as a run by its host is (nbt/process/app_output.hpp): what it wrote, and
 * notes on how the check went. The worker runs the check itself, without a
 * host, so before this the run left nothing there. In the car (dev58 to dev61)
 * every application's log read "No log yet" after an install, and Doom crashed
 * about a second into a run that may well have been this check. */

#include "nbt/fs.h"
#include "nbt/package.h"
#include "nbt/process/app_output.hpp"
#include "nbt/process/child_process.hpp"

namespace nbt {
namespace worker {

class InstallCheckLog {
public:
    InstallCheckLog() : output_(NULL, ""), fs_(NULL), active_(false), write_(-1) {
        app_[0] = 0;
    }
    ~InstallCheckLog();
    InstallCheckLog(const InstallCheckLog &) = delete;
    InstallCheckLog &operator=(const InstallCheckLog &) = delete;

    /* The descriptor to give the candidate as stdout and stderr, or -1 (its
     * output then goes where the worker's goes). */
    int begin(nbt_fs *fs, const char *app);
    /* After the start attempt: `child` running, or the start failed (errno). */
    void started(const char *version, uint64_t sequence, const process::ChildProcess &child);
    void notStarted(const char *version, int error);
    /* Copies what the candidate wrote so far into its log. */
    void drain();
    void passed();
    /* The check failed: `child` ended (how), or still ran when it gave up.
     * A crash (log::describeEnding) leaves a crash report too. */
    void failed(const process::ChildProcess &child, bool timedOut);
    /* What the candidate wrote last, then lets the log go. */
    void end();
    bool active() const {
        return active_;
    }

private:
    void closeWrite();

    process::AppOutput output_;
    nbt_fs *fs_;
    bool active_;
    int write_;
    char app_[NBT_ID_MAX + 1];
};

} // namespace worker
} // namespace nbt

#endif
