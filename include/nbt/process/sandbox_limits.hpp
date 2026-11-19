#ifndef NBT_PROCESS_SANDBOX_LIMITS_HPP
#define NBT_PROCESS_SANDBOX_LIMITS_HPP

/* An application's resource limits, prepared before it starts and applied in
 * the process that then becomes the application. The host build applies them
 * in its forked child (child_sandbox.cpp). The car cannot fork (QNX 6.5's
 * fork() fails once the process has threads), and spawn()'s struct
 * inheritance has no limits, so the host spawns its own binary as a wrapper
 * (--app-exec): it sets the limits and execve()s the application, which keeps
 * the wrapper's pid, process group, priority and descriptors. */

#include "nbt/process/child_spawn.hpp"

#include <stddef.h>
#include <sys/resource.h>

namespace nbt {
namespace process {

struct PreparedLimit {
    int resource;
    const char *name;
    bool bytes;
    struct rlimit value;
};

struct PreparedSandbox {
    PreparedLimit limits[6];
    size_t limitCount;
    bool setNice;
    int niceValue;
    int dropUid;
    int dropGid;
    const char *chrootDir;
};

/* Limits for `sb`; `fdCount` descriptors are passed to the child. Only calls
 * that are safe before exec happen in applyLimits. */
void prepareSandbox(const ChildSandbox &sb, unsigned fdCount, PreparedSandbox &prep);
/* Sets each prepared limit; returns how many failed (errno of the last one in
 * *error). Async-signal-safe. */
unsigned applyLimits(const PreparedSandbox &prep, int *error);

/* The wrapper's command line: `self --app-exec PATH ARGV0 ARGS...`. The
 * host sets `self` (its own absolute path); without it, the car starts
 * applications with no limits. */
void setAppExecWrapper(const char *self);
const char *appExecWrapper();
/* nbt-managerd --app-exec: applies defaultAppSandbox()'s limits, reports them
 * on standard error (the application's log), then execve()s PATH. Returns
 * only on failure (127). */
int appExecMain(int argc, char **argv);

} // namespace process
} // namespace nbt

#endif
