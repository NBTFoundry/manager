// SPDX-License-Identifier: MPL-2.0
// Crash reports (nbt/log/crash_report.hpp): slots, numbering, listing, clearing,
// the run's output kept in memory, and the endings that count as crashes.
#include "nbt/fs.h"
#include "nbt/log/app_log.hpp"
#include "nbt/log/crash_report.hpp"
#include "nbt/process.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static char root[256];

static void makeDir(const char *relative) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    CHECK(mkdir(path, 0700) == 0);
}

static size_t slurp(const char *relative, char *out, size_t capacity) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    FILE *f = fopen(path, "rb");
    CHECK(f);
    const size_t n = fread(out, 1, capacity - 1, f);
    CHECK(!ferror(f) && fclose(f) == 0);
    out[n] = 0;
    return n;
}

static bool exists(const char *relative) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    return access(path, F_OK) == 0;
}

static int report(nbt_fs *fs, const char *app, const char *ending) {
    static const char output[] = "line one\nline two\n";
    const nbt::log::CrashFacts facts = {app, "1.2.3", 1002003, "by hand", 42, 1500, ending};
    const nbt::log::CrashSection section = {"Output", {output, NULL}, {sizeof(output) - 1, 0}};
    return nbt::log::writeCrashReport(fs, facts, &section, 1, NULL);
}

static void slotsAndNumbers(nbt_fs *fs) {
    nbt::log::CrashEntry entries[nbt::log::kCrashSlots];
    CHECK(nbt::log::listCrashReports(fs, "test", entries, nbt::log::kCrashSlots) == 0);
    char ending[32];
    for (unsigned i = 0; i < 7; ++i) {
        snprintf(ending, sizeof(ending), "ended by signal %u", i + 1);
        // Five slots: 0 to 4, then the oldest (report 1 in slot 0, report 2 in slot 1).
        CHECK(report(fs, "test", ending) == static_cast<int>(i < 5 ? i : i - 5));
    }
    CHECK(!exists("state/test/crash.next"));
    const int count = nbt::log::listCrashReports(fs, "test", entries, nbt::log::kCrashSlots);
    CHECK(count == 5);
    for (int i = 0; i < count; ++i) { // newest first: reports 7 to 3
        CHECK(entries[i].number == static_cast<unsigned long>(7 - i));
        CHECK(!strcmp(entries[i].app, "test") && !strcmp(entries[i].version, "1.2.3"));
        CHECK(strlen(entries[i].time) == 19);
        snprintf(ending, sizeof(ending), "ended by signal %d", 7 - i);
        CHECK(!strcmp(entries[i].ending, ending));
    }
    CHECK(entries[0].slot == 1 && entries[1].slot == 0 && entries[2].slot == 4);
    CHECK(nbt::log::listCrashReports(fs, "test", entries, 2) == 2 && entries[1].number == 6);

    char text[2048];
    slurp("state/test/crash.1", text, sizeof(text));
    const char *expected = "NBT Manager crash report\n"
                           "Application: test\n"
                           "Version: 1.2.3\n"
                           "Sequence: 1002003\n"
                           "Report: 7\n"
                           "Time: ";
    CHECK(!strncmp(text, expected, strlen(expected)));
    const char *rest = strstr(text, "\nStarted: ");
    CHECK(rest && !strncmp(rest,
                           "\nStarted: by hand\nProcess: 42\nRan: 1.5 s\nEnded: ended by signal 7\n"
                           "\nOutput:\nline one\nline two\n\n",
                           strlen("\nStarted: by hand\nProcess: 42\nRan: 1.5 s\nEnded: ended by "
                                  "signal 7\n\nOutput:\nline one\nline two\n\n")));
    char path[512];
    snprintf(path, sizeof(path), "%s/state/test/crash.1", root);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);

    // Something else in a slot is not listed, and is the first to be replaced.
    FILE *stray = fopen(path, "w");
    CHECK(stray && fputs("not a report\n", stray) >= 0 && fclose(stray) == 0);
    CHECK(nbt::log::listCrashReports(fs, "test", entries, nbt::log::kCrashSlots) == 4);
    CHECK(report(fs, "test", "exited with status 139") == 1);
    CHECK(nbt::log::listCrashReports(fs, "test", entries, 1) == 1 && entries[0].number == 7 &&
          !strcmp(entries[0].ending, "exited with status 139"));

    // The manager's own reports live in state/manager.
    CHECK(report(fs, "manager", "manager GUI terminated by signal 11") == 0);
    CHECK(exists("state/manager/crash.0"));

    CHECK(nbt::log::clearCrashReports(fs, "test") == 0);
    CHECK(nbt::log::listCrashReports(fs, "test", entries, nbt::log::kCrashSlots) == 0);
    CHECK(nbt::log::listCrashReports(fs, "manager", entries, nbt::log::kCrashSlots) == 1);
    CHECK(report(fs, "test", "again") == 0);
    CHECK(nbt::log::listCrashReports(fs, "test", entries, 1) == 1 && entries[0].number == 1);
}

static void endings() {
    char text[128];
    const nbt::log::CrashEnding clean = {0, 0, 0}, failed = {3, 0, 0}, guest = {139, 0, 0},
                                hung = {124, 0, 0}, segv = {0, SIGSEGV, 0},
                                heartbeat = {0, 0, NBT_PROCESS_FAULT_HEARTBEAT};
    CHECK(!nbt::log::describeEnding(clean, text, sizeof(text)) &&
          !strcmp(text, "exited with status 0"));
    CHECK(!nbt::log::describeEnding(failed, text, sizeof(text)) &&
          !strcmp(text, "exited with status 3"));
    CHECK(nbt::log::describeEnding(guest, text, sizeof(text)));
    CHECK(nbt::log::describeEnding(hung, text, sizeof(text)));
    CHECK(nbt::log::describeEnding(segv, text, sizeof(text)) &&
          !strcmp(text, "ended by signal 11 (SIGSEGV, invalid memory access)"));
    CHECK(nbt::log::describeEnding(heartbeat, text, sizeof(text)) &&
          !strcmp(text, "stopped: no heartbeat ('R' on fd 3) for 3 s"));
    nbt::log::sequenceVersion(3001, text, sizeof(text));
    CHECK(!strcmp(text, "0.3.1"));
    nbt::log::sequenceVersion(1063003, text, sizeof(text));
    CHECK(!strcmp(text, "1.63.3"));
}

/* The run's newest bytes, kept even when the log file cannot be written. */
static void runTail() {
    nbt::log::AppLog log(NULL, "test");
    const char *first, *second;
    size_t firstSize, secondSize;
    log.beginRun();
    log.runTail(first, firstSize, second, secondSize);
    CHECK(firstSize == 0 && secondSize == 0);
    log.write("hello\n", 6);
    log.runTail(first, firstSize, second, secondSize);
    CHECK(firstSize == 6 && !memcmp(first, "hello\n", 6) && secondSize == 0);
    static char chunk[nbt::log::kRunTailMax - 8];
    memset(chunk, 'x', sizeof(chunk));
    log.write(chunk, sizeof(chunk));
    log.write("end\n", 4); // 2 bytes too many: "llo\n", the chunk, then "end\n"
    log.runTail(first, firstSize, second, secondSize);
    CHECK(firstSize + secondSize == nbt::log::kRunTailMax);
    static char whole[nbt::log::kRunTailMax];
    memcpy(whole, first, firstSize);
    memcpy(whole + firstSize, second, secondSize);
    CHECK(!memcmp(whole, "llo\nx", 5));
    CHECK(!memcmp(whole + sizeof(whole) - 5, "xend\n", 5));
    static char large[nbt::log::kRunTailMax + 10];
    memset(large, 'y', sizeof(large));
    large[sizeof(large) - 1] = 'z';
    log.write(large, sizeof(large));
    log.runTail(first, firstSize, second, secondSize);
    CHECK(firstSize == nbt::log::kRunTailMax && secondSize == 0 && first[firstSize - 1] == 'z');
    log.beginRun(); // a new run starts empty
    log.runTail(first, firstSize, second, secondSize);
    CHECK(firstSize == 0 && secondSize == 0);
}

int main() {
    snprintf(root, sizeof(root), "/tmp/nbt-crash-report-test.%ld", static_cast<long>(getpid()));
    CHECK(mkdir(root, 0700) == 0);
    makeDir("state");
    makeDir("state/test");
    makeDir("state/manager");
    nbt_fs fs;
    char error[128];
    CHECK(nbt_fs_init(&fs, root, error, sizeof(error)) == 0);
    slotsAndNumbers(&fs);
    endings();
    runTail();
    char command[400];
    snprintf(command, sizeof(command), "rm -rf '%s'", root);
    CHECK(system(command) == 0);
    puts("crash report tests passed");
    return 0;
}
