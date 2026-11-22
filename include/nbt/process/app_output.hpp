// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_APP_OUTPUT_HPP
#define NBT_PROCESS_APP_OUTPUT_HPP

/* An application's standard output and error, drained into its log
 * (nbt/log/app_log.hpp) with notes on how it started and ended: from a pipe
 * whose write end becomes its host's descriptors 1 and 2 (start), or is given
 * to the worker's install health check as the candidate's (open). */

#include "nbt/fs.h"
#include "nbt/log/app_log.hpp"
#include "nbt/log/crash_report.hpp"
#include "nbt/package.h"
#include "nbt/process.h"

namespace nbt {
namespace process {

class AppOutput {
public:
    AppOutput(nbt_fs *fs, const char *app)
        : log_(fs, app), fd_(-1), reportFd_(-1), sequence_(0), pid_(0), startedMs_(0),
          start_(NULL) {
        ending_[0] = version_[0] = 0;
    }
    ~AppOutput();
    AppOutput(const AppOutput &) = delete;
    AppOutput &operator=(const AppOutput &) = delete;

    /* Redirects descriptors 1 and 2 into the pipe; false leaves them as they
     * were (the debug log while it is on, else /dev/null). */
    bool start();
    /* Creates the pipe and returns its write end (close-on-exec, the caller's
     * to close once the application has it), or -1. */
    int open();
    /* The pipe's read end to poll, or -1. */
    int fd() const {
        return fd_;
    }
    /* Copies what the application wrote so far into its log. */
    void drain();
    /* SIGTERM, SIGKILL after the grace period, as nbt_process_stop, reading
     * the output meanwhile: what the application writes as it ends (a
     * daemon's log, dev122) filled the pipe, blocked it and was lost with the
     * SIGKILL. */
    void stopChild(nbt_process &child);
    log::AppLog &log() {
        return log_;
    }
    /* Notes how `child` ended: its exit status or signal, or the contract it
     * broke (`child.fault`), after draining what it wrote last. True when
     * that is a crash (log::describeEnding); ending() then says how. */
    bool ended(const nbt_process &child);
    const char *ending() const {
        return ending_;
    }
    /* A run of `version` starts as `pid` (`start`: "by hand", "autostart",
     * "install check"): its log keeps the newest output (AppLog::beginRun) for
     * a crash report. */
    void beginRun(const char *version, uint64_t sequence, long pid, const char *start);
    /* A crash report of this run (log::writeCrashReport): how it ended and its
     * output. With `systemLog`, finishReport() adds the system log's lines. */
    int report(nbt_fs *fs, const char *app, bool systemLog);
    /* Adds those lines (sloginfo: up to seconds) and closes the report. */
    void finishReport();
    /* Closes the pipe; the log becomes `app`'s (AppLog::retarget). */
    void retarget(nbt_fs *fs, const char *app);

private:
    log::AppLog log_;
    int fd_, reportFd_;
    uint64_t sequence_;
    long pid_;
    uint64_t startedMs_; // UINT64_MAX: unknown
    const char *start_;
    char ending_[128];
    char version_[NBT_VERSION_MAX + 1];
};

} // namespace process
} // namespace nbt

#endif
