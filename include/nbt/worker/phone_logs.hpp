#ifndef NBT_WORKER_PHONE_LOGS_HPP
#define NBT_WORKER_PHONE_LOGS_HPP

/* The logs a paired phone reads (PROTOCOL.md, "Logs"): an application's own
 * log (nbt/log/app_log.hpp: output.log.1, then output.log) and the iDrive
 * processor's system log, which `sloginfo` prints into an unlinked temporary
 * file while the worker carries on. Replies are streamed from these files,
 * never held in memory. */

#include "nbt/core/unique_fd.hpp"
#include "nbt/storage/managed_root.hpp"

#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace worker {

/* The newest part of the system log sent; sloginfo printed 5.2 MB in the car. */
static const uint64_t kSystemLogMax = UINT64_C(8) * 1024 * 1024;
/* How long sloginfo may take, and the most it may write before it is stopped. */
static const uint64_t kSystemLogWaitMs = 10000;
static const uint64_t kSystemLogFileMax = UINT64_C(32) * 1024 * 1024;

struct LogReply {
    LogReply() : child(-1), startedMs(0) {
        sizes[0] = sizes[1] = 0;
    }
    ~LogReply() {
        reset();
    }
    LogReply(const LogReply &) = delete;
    LogReply &operator=(const LogReply &) = delete;

    /* Stops a running sloginfo and closes the files. */
    void reset();
    uint64_t total() const {
        return sizes[0] + sizes[1];
    }
    bool pending() const {
        return child > 0;
    }

    core::UniqueFd files[2];
    uint64_t sizes[2];
    pid_t child;
    uint64_t startedMs;
};

/* `body` is "<app>\n". 200 with the files to send (none when the application
 * has written nothing yet), 400 for an unknown application or the manager. */
int phoneAppLog(storage::ManagedRoot &root, const char *body, LogReply &reply);
/* `body` is "<app>\n": empties output.log (its host keeps appending to it) and
 * removes output.log.1. 200, or 400 as phoneAppLog. */
int phoneClearAppLog(storage::ManagedRoot &root, const char *body);
/* Starts sloginfo: 202 while it runs, 503 when it cannot start. */
/* The manager's debug log: debug.log.1 then debug.log, the newest
 * kManagerLogMax bytes of each, raw. Empty (200) while the debug log is off. */
static const uint64_t kManagerLogMax = 4u * 1024u * 1024u;
int phoneManagerLog(storage::ManagedRoot &root, LogReply &reply);

int phoneSystemLogStart(uint64_t now, LogReply &reply);
/* After a start: 202 while sloginfo runs, 200 once files[0] holds its output
 * (its newest kSystemLogMax bytes), 503 when it failed or took too long. */
int phoneSystemLogPoll(uint64_t now, LogReply &reply);

} // namespace worker
} // namespace nbt

#endif
