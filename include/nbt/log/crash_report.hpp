// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LOG_CRASH_REPORT_HPP
#define NBT_LOG_CRASH_REPORT_HPP

/* Crash reports (PROTOCOL.md, "Crash reports"): when an application crashes
 * (a signal, a broken heartbeat contract, an nbt-runner failure status) its
 * host writes one, and the launcher writes one when NBT Manager's own GUI or
 * worker ends unexpectedly. Each is a text file of its own,
 * state/<app>/crash.<slot> (state/manager for the manager), apart from the
 * rotating logs, so a crash stays readable after later runs: a header of
 * "Key: value" lines, then sections (the run's output, the system log's fault
 * lines). Five slots per application; a new report takes an empty slot, else
 * the oldest. Paired phones list, read and clear them. Write errors go to the
 * debug log: a report never stops anything. */

#include "nbt/fs.h"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace log {

static const unsigned kCrashSlots = 5;
/* The header a phone listing reads; reports start with it. */
static const size_t kCrashHeaderMax = 1024;
static const char kCrashTitle[] = "NBT Manager crash report";
/* The system log's newest fault lines a report keeps, and how long sloginfo may take. */
static const unsigned kCrashSystemLines = 8;
static const uint64_t kCrashSystemWaitMs = 10000;

struct CrashFacts {
    const char *app;     // an application, or "manager"
    const char *version; // its version
    uint64_t sequence;
    const char *start;  // "by hand", "autostart", "install check"; NULL: not said
    long pid;           // 0: not said
    uint64_t ranMs;     // UINT64_MAX: not said
    const char *ending; // how it ended, as its log's note says
};

/* A section of a report: a title line, then up to two pieces of text. */
struct CrashSection {
    const char *title;
    const char *data[2];
    size_t size[2];
};

/* Writes a report and returns its slot, or -1. With `keep`, *keep gets the
 * published report's descriptor (to append the system log once the
 * application's locks are released; the caller closes it), else -1. */
int writeCrashReport(nbt_fs *fs, const CrashFacts &facts, const CrashSection *sections,
                     size_t count, int *keep);

/* Appends the system log's newest kCrashSystemLines lines about faults
 * (sloginfo, at most kCrashSystemWaitMs) to an open report, then syncs it. */
void appendCrashSystemLog(int fd);

/* "ended by signal 11 (SIGSEGV, ...)" and the like into `text`: how `child`
 * ended, as the application log's note says. True when that is a crash: a
 * signal, a broken contract (no ready byte, no heartbeat, ...), or exit status
 * 124 and above (nbt-runner: a hung or unloadable payload, 128 + the signal
 * that ended a guest). Other exit statuses are the application's own. */
struct CrashEnding {
    int exitStatus;
    int exitSignal;
    int fault;
};
bool describeEnding(const CrashEnding &ending, char *text, size_t size);

struct CrashEntry {
    char app[NBT_APP_NAME_MAX + 1];
    unsigned slot;
    unsigned long number; // goes up by one per report of this application
    char time[20];        // "2026-11-22 14:03:11", the car's local time
    char version[49];
    char ending[81];
};

/* `app`'s reports, newest first: their count, or -1 when one cannot be read. */
int listCrashReports(nbt_fs *fs, const char *app, CrashEntry *out, size_t max);

/* Removes all of `app`'s reports: 0, or -1. */
int clearCrashReports(nbt_fs *fs, const char *app);

/* "0.3.1" for release sequence 3001 (X * 1000000 + Y * 1000 + Z). */
void sequenceVersion(uint64_t sequence, char *out, size_t size);

} // namespace log
} // namespace nbt

#endif
