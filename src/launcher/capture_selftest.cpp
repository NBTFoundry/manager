/* Drives the capture shim the way the GUI and iDrive will, before the install
 * names it in starter.cfg: the GUI's own writers for the control and heartbeat
 * files, controller messages as the MOST client sends them, and a counting
 * forward target. The shim's test files are private (state/manager); iDrive's
 * /dev/shmem files are never touched. */
#include "nbt/launcher/capture_selftest.hpp"

#include "nbt/capture/control.h"
#include "nbt/capture/diag.h"
#include "nbt/gui/hmi_hold.hpp"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace launcher {
namespace {

typedef void (*DsiFn)(void *, void *);
typedef int (*BeginFn)(const char *, DsiFn, uint64_t (*)(void), int *);

uint64_t g_now = 10000;
unsigned g_forwarded = 0;

uint64_t clockNow() {
    return g_now;
}
void forwardCounted(void *, void *) {
    ++g_forwarded;
}

enum Key { Knob, Back, Favourite };

/* A knob turn (E5 0x0C46), hard BACK (E5 0x0C47) or favourite key 1 (centre
 * stack 0x15 0x0C02) as the MOST server delivers it. */
void buildStream(unsigned char buf[64], nbt_dsi_c_stream &s, Key key, uint8_t held) {
    const bool back = key != Knob;
    memset(buf, 0, 64);
    buf[8] = 0x01;
    buf[11] = 0x80;
    unsigned char *m = buf + 16;
    m[4] = 2;
    m[8] = 1;
    m[16] = key == Favourite ? NBT_CAPTURE_FBLOCK_CENTER_STACK : 0xE5;
    m[17] = key == Favourite ? 0x01 : 0x03;
    m[18] = key == Favourite ? 0x02 : (back ? 0x47 : 0x46);
    m[19] = 0x0C;
    m[20] = 0x0C;
    m[28] = back ? 7 : 5;
    if (key == Favourite) {
        m[32] = held;
    } else if (back) {
        m[32 + 1] = held;
    } else {
        m[32 + 2] = 1;
    }
    memset(&s, 0, sizeof(s));
    s.capacity = 64;
    s.populated = back ? 55 : 53;
    s.buffer = buf;
}

class Run {
public:
    Run(DsiFn dsi, const char *dir, char *detail, size_t size)
        : dsi_(dsi), detail_(detail), size_(size), cursor_(0), seen_(0), step_(0) {
        snprintf(control_, sizeof(control_), "%s/capture-selftest.control", dir);
        snprintf(live_, sizeof(live_), "%s/capture-selftest.live", dir);
        snprintf(diag_, sizeof(diag_), "%s/capture-selftest.diag", dir);
        memset(most_, 0, sizeof(most_));
        memcpy(most_ + 0x20, "MOSTSubSystem.DSYSMOSTServer", 29);
        memset(other_, 0, sizeof(other_));
        memcpy(other_ + 0x20, "OtherService.Server", 20);
    }
    bool control(uint8_t state, uint64_t generation, int32_t pid) {
        nbt_capture_control ctrl;
        nbt_capture_control_init(&ctrl, pid, 11u, generation, NULL);
        ctrl.state = state;
        ctrl.watchdog_ms = NBT_CAPTURE_DEFAULT_WATCHDOG_MS; // as the GUI's CaptureLink::sync
        ctrl.escape_enabled = 1u;
        g_now += NBT_CAPTURE_CACHE_MS; // the shim re-reads after its cache window
        return nbt_capture_control_write(control_, &ctrl) == 0 || fail("control write");
    }
    bool beat(pid_t pid) {
        return gui::holdLivenessWrite(live_, pid, g_now) == 0 || fail("heartbeat write");
    }
    /* Sends one message; checks forwarding and the newest diagnostic kind (0: none). */
    bool expect(const char *what, Key key, uint8_t held, bool fromMost, bool forwarded,
                uint8_t kind) {
        unsigned char buf[64];
        nbt_dsi_c_stream s;
        buildStream(buf, s, key, held);
        ++step_;
        const unsigned before = g_forwarded;
        dsi_(fromMost ? most_ : other_, &s);
        const bool sent = g_forwarded == before + 1;
        Records records = {0, 0};
        nbt_capture_diag_shm snap;
        const bool readable = nbt_capture_diag_read(diag_, &snap) == 0;
        if (readable) {
            uint32_t missed = 0;
            nbt_capture_diag_drain(&snap, &cursor_, &missed, &Run::sink, &records);
        }
        const uint8_t last = records.last;
        seen_ = records.seen;
        if (sent != forwarded || (kind != 0xff && last != kind)) {
            char got[24];
            snprintf(got, sizeof(got), readable ? "record %u" : "no records", last);
            snprintf(detail_, size_, "step %u (%s): %s, %s; expected %s, record %u", step_, what,
                     sent ? "forwarded" : "held", got, forwarded ? "forwarded" : "held", kind);
            return false;
        }
        return true;
    }
    /* Whether the last message's step also recorded `kind`. */
    bool saw(uint8_t kind, const char *what) {
        if (seen_ & (1u << kind)) {
            return true;
        }
        snprintf(detail_, size_, "step %u (%s): no record %u", step_, what, kind);
        return false;
    }
    bool fail(const char *what) {
        snprintf(detail_, size_, "%s failed", what);
        return false;
    }
    void cleanup() {
        (void)unlink(control_);
        (void)unlink(live_);
        (void)unlink(diag_);
    }

private:
    struct Records {
        uint8_t last;
        uint32_t seen;
    };
    static void sink(const nbt_capture_diag_slot *slot, void *user) {
        Records *records = static_cast<Records *>(user);
        if (slot->kind != NBT_CAPTURE_DIAG_INIT && slot->kind < 32) {
            records->last = slot->kind;
            records->seen |= 1u << slot->kind;
        }
    }
    DsiFn dsi_;
    char *detail_;
    size_t size_;
    uint32_t cursor_;
    uint32_t seen_;
    unsigned step_;
    char control_[300], live_[300], diag_[300];
    unsigned char most_[0xE0], other_[0xE0];
};

bool scenario(Run &r, bool observer) {
    const pid_t self = getpid();
    const uint8_t held = observer ? NBT_CAPTURE_DIAG_OBSERVE : NBT_CAPTURE_DIAG_SUPPRESS;
    const int32_t gone = 0x7ffffff0; // no such process: a GUI that died
    return r.expect("capture off", Knob, 0, true, true, 0) &&
           // On with a fresh heartbeat: knob and BACK held (observer: recorded only).
           r.control(NBT_CAPTURE_ON, 2, self) && r.beat(self) &&
           r.expect("on, knob", Knob, 0, true, observer, held) &&
           r.expect("on, BACK pressed", Back, 1, true, observer, held) &&
           r.expect("on, BACK released", Back, 0, true, observer, held) &&
           // Favourite keys: presses and releases alike, or iDrive runs the favourite.
           r.expect("on, favourite pressed", Favourite, 1, true, observer, held) &&
           r.expect("on, favourite released", Favourite, 0, true, observer, held) &&
           r.expect("on, another client", Knob, 0, false, true, 0) &&
           // BACK goes down captured, and its release is never seen captured.
           r.expect("on, BACK pressed again", Back, 1, true, observer, held) &&
           // No heartbeat for 1.5 s: released.
           (g_now += NBT_CAPTURE_DEFAULT_WATCHDOG_MS, true) &&
           r.expect("heartbeat stopped", Knob, 0, true, true, 0xff) &&
           // ... and the BACK pressed before stops counting toward the escape.
           r.saw(NBT_CAPTURE_DIAG_HOLD_DROPPED, "heartbeat stopped") && r.beat(self) &&
           // Switched off by the GUI: released.
           r.control(NBT_CAPTURE_OFF, 3, self) && r.beat(self) &&
           r.expect("switched off", Knob, 0, true, true, 0xff) &&
           // On again more than 5 s after that BACK: still held, no escape.
           (g_now += NBT_CAPTURE_ESCAPE_HOLD_MS, true) &&
           // Then BACK held 5 s: the escape releases this generation.
           r.control(NBT_CAPTURE_ON, 4, self) && r.beat(self) &&
           r.expect("on again", Knob, 0, true, observer, held) &&
           r.expect("BACK held", Back, 1, true, observer, held) &&
           (g_now += NBT_CAPTURE_ESCAPE_HOLD_MS, true) && r.beat(self) &&
           // The release that completes the escape is itself held back by the active build.
           r.expect("BACK released after 5 s", Back, 0, true, observer,
                    observer ? NBT_CAPTURE_DIAG_OBSERVE : NBT_CAPTURE_DIAG_ESCAPE) &&
           // Re-read with capture still on: the escape stays for this generation.
           (g_now += NBT_CAPTURE_CACHE_MS, true) && r.beat(self) &&
           r.expect("after the escape", Knob, 0, true, true, NBT_CAPTURE_DIAG_OBSERVE) &&
           // A record from a GUI that no longer exists: released.
           r.control(NBT_CAPTURE_ON, 5, gone) && r.beat(gone) &&
           r.expect("GUI gone", Knob, 0, true, true, 0xff);
}

} // namespace

int captureSelfTest(const char *shim, const char *dir, char *detail, size_t size, bool *observer) {
    detail[0] = 0;
    void *library = dlopen(shim, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        const char *why = dlerror();
        snprintf(detail, size, "dlopen: %s", why ? why : "failed");
        return -1;
    }
    BeginFn begin = NULL;
    DsiFn dsi = NULL;
    void *b = dlsym(library, "nbt_capture_selftest_begin");
    void *d = dlsym(library, "DSIClientDataResponse");
    memcpy(&begin, &b, sizeof(begin));
    memcpy(&dsi, &d, sizeof(dsi));
    Run run(dsi, dir, detail, size);
    run.cleanup();
    int mode = 0;
    int rc = -1;
    if (!begin || !dsi) {
        snprintf(detail, size, "the shim has no self-test entry");
    } else if (begin(dir, &forwardCounted, &clockNow, &mode)) {
        snprintf(detail, size, "the shim refused the self-test");
    } else if (scenario(run, mode != 0)) {
        *observer = mode != 0;
        rc = 0;
    }
    run.cleanup();
    // Not closed: the shim keeps its once-only state and mapping for the process's life.
    return rc;
}

} // namespace launcher
} // namespace nbt
