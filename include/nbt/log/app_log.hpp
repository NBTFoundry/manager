#ifndef NBT_LOG_APP_LOG_HPP
#define NBT_LOG_APP_LOG_HPP

/* An application's own log, state/<app>/output.log: everything the
 * application writes to its standard output and error, and its host's notes
 * on how it started and ended ("--- <local time> <note>" lines). Unlike the
 * manager's debug log it is always on, so a crash leaves a trace. At
 * kAppLogMax bytes the file moves to output.log.1 (replacing the previous
 * one), so an application keeps at most twice that. Paired phones read both
 * (POST /v1/logs/app). Write errors drop the data: logging never stops an
 * application.
 *
 * The car's file system keeps writes in memory until its next snapshot, and
 * an iDrive restart (which a crashing application can cause) loses them. So
 * notes are synced at once, and output at most kAppLogSyncMs after it was
 * written. */

#include "nbt/fs.h"
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace log {

static const size_t kAppLogMax = 64 * 1024;
static const uint64_t kAppLogSyncMs = 1000;

class AppLog {
public:
    AppLog(nbt_fs *fs, const char *app)
        : fs_(fs), app_(app), fd_(-1), dirty_(false), seekEnd_(false), syncedMs_(0), failures_(0) {
    }
    ~AppLog();
    AppLog(const AppLog &) = delete;
    AppLog &operator=(const AppLog &) = delete;

    void write(const char *data, size_t size);
    /* A note, synced before it returns. */
    void note(const char *format, ...) __attribute__((format(printf, 2, 3)));
    /* Syncs output written since the last sync, once kAppLogSyncMs have passed. */
    void syncDue();
    /* Syncs and closes this log; later writes go to `app`'s (`app` must outlive them). */
    void retarget(nbt_fs *fs, const char *app);

private:
    enum Failure { Open = 1, Stat = 2, Write = 4, Sync = 8, Rotate = 16 };
    void failed(Failure operation, const char *name, int error);
    bool open();
    int fallbackOpen(const char *path, int checkedError);
    bool acceptable(int fd) const;
    bool rotate();
    void sync();

    nbt_fs *fs_;
    const char *app_;
    int fd_;
    bool dirty_;
    bool seekEnd_; // opened without O_APPEND: seek to the end before each write
    uint64_t syncedMs_;
    unsigned failures_; // report each kind of failure once per host
};

/* "SIGSEGV" and the like for the signals an application dies of; NULL for
 * others. */
const char *signalName(int number);

} // namespace log
} // namespace nbt

#endif
