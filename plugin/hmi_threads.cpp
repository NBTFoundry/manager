// Thread snapshot of the HMI process for the debug log (hmi_threads.hpp).
// QNX 6.5: DCMD_PROC_TIDSTATUS on /proc/<pid>/as returns the next thread's
// debug_thread_t; the HMI's own threads are readable from inside it. dladdr and
// pthread_getname_np/setname_np are looked up at run time, so a libc without
// them leaves the names out instead of failing the plugin's load.
#include "hmi_threads.hpp"

#include "nbt/log.h"

#include <devctl.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/debug.h>
#include <sys/procfs.h>
#include <unistd.h>

#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)-2) // QNX value
#endif

namespace nbt {
namespace plugin {

namespace {
typedef int (*GetName)(pthread_t, char *, int);
typedef int (*SetName)(pthread_t, const char *);
typedef int (*DlAddr)(void *, Dl_info *);

const char *const kStates[] = {
    "DEAD",    "RUNNING",    "READY",    "STOPPED",    "SEND",        "RECEIVE",   "REPLY",
    "STACK",   "WAITTHREAD", "WAITPAGE", "SIGSUSPEND", "SIGWAITINFO", "NANOSLEEP", "MUTEX",
    "CONDVAR", "JOIN",       "INTR",     "SEM",        "WAITCTX",     "NET_SEND",  "NET_REPLY"};
const unsigned kStateMutex = 13, kStateCondvar = 14, kStateSem = 17;
const unsigned kMaxThreads = 128;
const unsigned kStackBytes = 1536;
const unsigned kStackHits = 15;
const unsigned kHitsPerLine = 5;

// "object+0xoffset", or the bare address when no loaded object holds it.
bool where(DlAddr dladdrFn, uintptr_t address, char *out, size_t size) {
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (!dladdrFn || !address || !dladdrFn(reinterpret_cast<void *>(address), &info) ||
        !info.dli_fname) {
        snprintf(out, size, "%08lx", static_cast<unsigned long>(address));
        return false;
    }
    const char *name = strrchr(info.dli_fname, '/');
    name = name ? name + 1 : info.dli_fname;
    snprintf(out, size, "%.24s+0x%lx", name,
             static_cast<unsigned long>(address - reinterpret_cast<uintptr_t>(info.dli_fbase)));
    return true;
}

void logStack(DlAddr dladdrFn, unsigned tid, const debug_thread_t &status) {
    const uintptr_t sp = static_cast<uintptr_t>(status.sp);
    const uintptr_t base = static_cast<uintptr_t>(status.stkbase);
    const uintptr_t top = base + status.stksize;
    if (!dladdrFn || !status.stksize || sp < base || sp >= top || (sp & 3u)) {
        return;
    }
    const uintptr_t end = top - sp > kStackBytes ? sp + kStackBytes : top;
    char line[NBT_LOG_LINE];
    size_t used = 0;
    unsigned hits = 0, onLine = 0;
    uintptr_t last = 0;
    for (uintptr_t at = sp; at + 4 <= end && hits < kStackHits; at += 4) {
        const uintptr_t value = *reinterpret_cast<const volatile uint32_t *>(at);
        char place[48];
        if (value < 0x100000u || value == last || !where(dladdrFn, value, place, sizeof(place))) {
            continue;
        }
        last = value;
        ++hits;
        int n = snprintf(line + used, sizeof(line) - used, " +%lx:%s",
                         static_cast<unsigned long>(at - sp), place);
        used += n > 0 && static_cast<size_t>(n) < sizeof(line) - used ? static_cast<size_t>(n) : 0;
        if (++onLine == kHitsPerLine) {
            nbt_logf("  tid %u stack%s", tid, line);
            used = 0;
            onLine = 0;
        }
    }
    if (onLine) {
        nbt_logf("  tid %u stack%s", tid, line);
    }
}
} // namespace

void nameThread(const char *name) {
    SetName setName = reinterpret_cast<SetName>(dlsym(RTLD_DEFAULT, "pthread_setname_np"));
    if (setName) {
        (void)setName(pthread_self(), name);
    }
}

void logHmiThreads(const char *why) {
    if (!nbt_log_enabled()) {
        return;
    }
    GetName getName = reinterpret_cast<GetName>(dlsym(RTLD_DEFAULT, "pthread_getname_np"));
    DlAddr dladdrFn = reinterpret_cast<DlAddr>(dlsym(RTLD_DEFAULT, "dladdr"));
    char path[32];
    snprintf(path, sizeof(path), "/proc/%d/as", static_cast<int>(getpid()));
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        nbt_logf("hmi threads (%s): cannot open the process (errno %d)", why, errno);
        return;
    }
    nbt_logf("hmi threads (%s): pid %d, logged from tid %d", why, static_cast<int>(getpid()),
             static_cast<int>(pthread_self()));
    unsigned next = 1, count = 0;
    while (count < kMaxThreads) {
        debug_thread_t status;
        memset(&status, 0, sizeof(status));
        status.tid = static_cast<pthread_t>(next);
        if (devctl(fd, DCMD_PROC_TIDSTATUS, &status, sizeof(status), 0) != EOK || status.tid <= 0 ||
            static_cast<unsigned>(status.tid) < next) {
            break;
        }
        const unsigned tid = static_cast<unsigned>(status.tid);
        next = tid + 1;
        ++count;
        char name[32] = "";
        if (getName) {
            (void)getName(tid, name, sizeof(name));
            name[sizeof(name) - 1] = 0;
        }
        const char *state =
            status.state < sizeof(kStates) / sizeof(kStates[0]) ? kStates[status.state] : "?";
        char blocked[64] = "";
        if (status.state == kStateMutex || status.state == kStateCondvar ||
            status.state == kStateSem) {
            const uintptr_t sync = status.blocked.sync.sync;
            if (sync && !(sync & 3u)) {
                const volatile uint32_t *words = reinterpret_cast<const volatile uint32_t *>(sync);
                snprintf(blocked, sizeof(blocked), " on %08lx [%08x %08x] %llu ms",
                         static_cast<unsigned long>(sync), words[0], words[1],
                         static_cast<unsigned long long>(status.nsec_since_block / 1000000u));
            }
        }
        char ip[48];
        where(dladdrFn, static_cast<uintptr_t>(status.ip), ip, sizeof(ip));
        nbt_logf("tid %u '%s' %s pri %u cpu %llu ms%s ip %s", tid, name, state, status.priority,
                 static_cast<unsigned long long>(status.sutime / 1000000u), blocked, ip);
        logStack(dladdrFn, tid, status);
    }
    close(fd);
    nbt_logf("hmi threads (%s): %u threads", why, count);
}

} // namespace plugin
} // namespace nbt
