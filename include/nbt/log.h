#ifndef NBT_LOG_H
#define NBT_LOG_H

/* Optional bench debug log shared by the manager GUI, the worker and the iDrive
 * plugin: manager/debug.log under the manager root. Logging is on only while
 * that file exists; logging never creates it. The desktop installer or the
 * manager's Settings (src/manager_settings.c) create it to switch logging on;
 * Settings switches it off by moving it to debug.log.1. Each line is
 *
 *   <seconds since boot>.<ms> <component>[<pid>] <message>
 *
 * Messages are cut at NBT_LOG_LINE bytes and bytes outside printable ASCII are
 * replaced by '?', so one call is always one line. Callers must not log
 * secrets (the session token, digests being approved) or package contents.
 *
 * The GUI and the worker open the file through the manager path policy and,
 * once it reaches NBT_LOG_MAX bytes, move it to manager/debug.log.1 (replacing
 * the previous one) and start a new file. The plugin, inside the HMI process,
 * opens the fixed path itself and never rotates: it drops lines while the file
 * is full. Every writer follows a rotation at its next line, and removing
 * debug.log switches logging off for writers that have it open. Processes check the size
 * independently, so a file can exceed the limit by one line per process writing at that moment.
 * Write errors (a full disk) drop the line and are otherwise ignored. All functions are
 * thread-safe; logging never blocks on anything but a local file write. */

#include "nbt/fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_LOG_MAX (128u * 1024u)
#define NBT_LOG_LINE 200u
#define NBT_LOG_PATH "/var/opt/nbt-manager/manager/debug.log"

/* Starts logging for `component` (letters and digits, at most 15) through the
 * path policy. Returns 1 when logging is on, 0 when the file is absent and -1
 * when it exists but cannot be used (wrong owner, mode, type or links). */
int nbt_log_open(nbt_fs *fs, const char *component);
/* The plugin's variant: a fixed absolute path, no rotation. Same results.
 * Calling it again while logging is on does nothing and returns 1. */
int nbt_log_open_path(const char *path, const char *component);
int nbt_log_enabled(void);
/* Changes each time this process starts writing to another file (switched
 * on, rotated, switched off and on): a component re-logs its context then, so
 * every fetched log starts with it. 0 while logging is off. */
unsigned nbt_log_file(void);
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void nbt_logf(const char *format, ...);
void nbt_log_close(void);

#ifdef __cplusplus
}
#endif
#endif
