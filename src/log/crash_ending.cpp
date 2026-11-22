// SPDX-License-Identifier: MPL-2.0
/* How a run ended, and whether that is a crash (nbt/log/crash_report.hpp). */
#include "nbt/log/app_log.hpp"
#include "nbt/log/crash_report.hpp"
#include "nbt/process.h"

#include <stdio.h>

namespace nbt {
namespace log {

void sequenceVersion(uint64_t sequence, char *out, size_t size) {
    snprintf(out, size, "%llu.%llu.%llu", static_cast<unsigned long long>(sequence / 1000000),
             static_cast<unsigned long long>(sequence / 1000 % 1000),
             static_cast<unsigned long long>(sequence % 1000));
}

bool describeEnding(const CrashEnding &ending, char *text, size_t size) {
    switch (ending.fault) {
    case NBT_PROCESS_FAULT_NOT_READY:
        snprintf(text, size, "stopped: it did not signal ready (write 'R' to fd 3) within 5 s");
        return true;
    case NBT_PROCESS_FAULT_HEARTBEAT:
        snprintf(text, size, "stopped: no heartbeat ('R' on fd 3) for 3 s");
        return true;
    case NBT_PROCESS_FAULT_PROTOCOL:
        snprintf(text, size, "stopped: it wrote something other than 'R' to fd 3");
        return true;
    case NBT_PROCESS_FAULT_RELEASED:
        snprintf(text, size,
                 "stopped: it closed fd 3 or released its lease on fd 4 but kept running");
        return true;
    default:
        break;
    }
    if (ending.exitSignal) {
        const char *name = signalName(ending.exitSignal);
        snprintf(text, size, "ended by signal %d%s%s%s", ending.exitSignal, name ? " (" : "",
                 name ? name : "", name ? ")" : "");
        return true;
    }
    snprintf(text, size, "exited with status %d", ending.exitStatus);
    return ending.exitStatus >= 124;
}

} // namespace log
} // namespace nbt
