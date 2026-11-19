// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/hmi_hold.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

enum {
    POLL = nbt::gui::HoldPoll,
    FOCUS = nbt::gui::HoldFocus,
    PARENT = nbt::gui::HoldToParent,
    SHOW = nbt::gui::HoldShow
};

static uint64_t now = 5000;

static unsigned poll_after(nbt::gui::HmiHold *h, uint64_t ms, int running) {
    now += ms;
    return h->poll(now, running);
}

/* iDrive reaches the parent state, the plugin holds it on the blank one. */
static void reach_blank(nbt::gui::HmiHold *h, int running) {
    h->focus(nbt::gui::HoldParent, 1, now + 30);
    CHECK(poll_after(h, nbt::gui::kHoldPollMs, running) == (FOCUS | POLL)); /* at once */
    h->focus(nbt::gui::HoldParent, 0, now + 30);
    h->focus(nbt::gui::HoldBlank, 1, now + 40);
}

/* Entry selected with no GUI: spawn, open the parent (iDrive ignores the
 * blank state's event from its list), hold, and once the GUI runs show it. */
static void open_manager(nbt::gui::HmiHold *h) {
    h->reset();
    CHECK(h->poll(now, false) == 0);
    CHECK(h->entry(now, false) == (nbt::gui::HoldSpawn | PARENT | POLL));
    CHECK(h->phase == nbt::gui::HoldStarting);
    reach_blank(h, 0);
    CHECK(poll_after(h, nbt::gui::kHoldPollMs, 0) == POLL && h->phase == nbt::gui::HoldStarting);
    CHECK(poll_after(h, nbt::gui::kHoldPollMs, 1) == (SHOW | POLL) &&
          h->phase == nbt::gui::HoldOpen);
    CHECK(poll_after(h, nbt::gui::kHoldPollMs, 1) == POLL); /* shown once */
}

static void test_back_is_held_and_exit_releases(void) {
    nbt::gui::HmiHold h;
    open_manager(&h);
    CHECK(poll_after(&h, 5000, 1) == POLL);
    /* BACK: iDrive pops the blank state to its parent; the plugin returns. */
    h.focus(nbt::gui::HoldBlank, 0, now);
    h.focus(nbt::gui::HoldParent, 1, now + 5);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (FOCUS | POLL));
    CHECK(poll_after(&h, 10, 1) == POLL); /* waits for iDrive */
    h.focus(nbt::gui::HoldParent, 0, now);
    h.focus(nbt::gui::HoldBlank, 1, now);
    CHECK(h.tries == 0 && poll_after(&h, nbt::gui::kHoldPollMs, 1) == POLL);
    /* Repeated BACK keeps being held. */
    for (int i = 0; i < 10; ++i) {
        h.focus(nbt::gui::HoldBlank, 0, now);
        h.focus(nbt::gui::HoldParent, 1, now);
        CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (FOCUS | POLL));
        h.focus(nbt::gui::HoldParent, 0, now);
        h.focus(nbt::gui::HoldBlank, 1, now);
    }
    CHECK(h.phase == nbt::gui::HoldOpen);
    /* The GUI exits: return iDrive to the entry's list, once. */
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 0) == nbt::gui::HoldRelease &&
          h.phase == nbt::gui::HoldIdle);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 0) == 0);

    /* Leaving by BACK from the manager's first screen: iDrive is on the parent. */
    open_manager(&h);
    h.focus(nbt::gui::HoldBlank, 0, now);
    h.focus(nbt::gui::HoldParent, 1, now);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 0) == nbt::gui::HoldRelease);
}

static void test_hard_key_steps_aside(void) {
    nbt::gui::HmiHold h;
    open_manager(&h);
    /* NAV, MEDIA, a warning or a camera view: iDrive leaves for its own state. */
    h.focus(nbt::gui::HoldBlank, 0, now);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == POLL && h.phase == nbt::gui::HoldOpen);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (nbt::gui::HoldAside | POLL) &&
          h.phase == nbt::gui::HoldSteppedAside);
    /* Aside: no focus events while the GUI stays (it does during an installation). */
    CHECK(poll_after(&h, 5000, 1) == POLL);
    /* The entry brings it back, once iDrive is held again. */
    CHECK(h.entry(now, true) == (PARENT | POLL) && h.phase == nbt::gui::HoldOpen);
    reach_blank(&h, 1);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (SHOW | POLL));
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == POLL);
    /* Aside again, then the GUI exits: iDrive stays where the driver went. */
    h.focus(nbt::gui::HoldBlank, 0, now);
    CHECK(poll_after(&h, 2 * nbt::gui::kHoldPollMs, 1) == (nbt::gui::HoldAside | POLL));
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 0) == 0 && h.phase == nbt::gui::HoldIdle);
}

static void test_refused_focus_and_failed_start(void) {
    nbt::gui::HmiHold h;
    h.reset();
    /* iDrive refuses the focus event (a popup is open): retry, then give up.
     * Never held, the GUI is never told to show. */
    CHECK(h.entry(now, false) == (nbt::gui::HoldSpawn | PARENT | POLL));
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == POLL);
    CHECK(poll_after(&h, 1000, 1) == (PARENT | POLL));
    CHECK(poll_after(&h, 1000, 1) == (PARENT | POLL));
    CHECK(poll_after(&h, 1000, 1) == (PARENT | POLL));
    CHECK(h.tries == nbt::gui::kHoldTries);
    CHECK(poll_after(&h, 1000, 1) == POLL && poll_after(&h, 5000, 1) == POLL);
    /* The parent opened but the blank state refused: retried, never shown. */
    h.reset();
    CHECK(h.entry(now, true) == (PARENT | POLL));
    h.focus(nbt::gui::HoldParent, 1, now);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (FOCUS | POLL));
    CHECK(poll_after(&h, 1000, 1) == (FOCUS | POLL));
    CHECK(poll_after(&h, 1000, 1) == (FOCUS | POLL));
    CHECK(poll_after(&h, 1000, 1) == POLL && poll_after(&h, 5000, 1) == POLL);
    h.reset();
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 0) == 0 && h.phase == nbt::gui::HoldIdle);

    /* The launcher never starts a GUI: the hold ends after the start timeout. */
    h.reset();
    CHECK(h.entry(now, false) & nbt::gui::HoldSpawn);
    h.focus(nbt::gui::HoldBlank, 1, now);
    CHECK(poll_after(&h, nbt::gui::kHoldStartMs - 1, 0) == POLL);
    /* Selecting the entry again while starting only holds again. */
    CHECK(h.entry(now, false) == (FOCUS | POLL) && h.phase == nbt::gui::HoldStarting);
    CHECK(poll_after(&h, 1, 0) == nbt::gui::HoldRelease && h.phase == nbt::gui::HoldIdle);

    /* Leaving while the GUI starts: it is told to step aside once it runs. */
    h.reset();
    CHECK(h.entry(now, false) & nbt::gui::HoldSpawn);
    h.focus(nbt::gui::HoldBlank, 1, now);
    h.focus(nbt::gui::HoldBlank, 0, now + 100);
    CHECK(poll_after(&h, 2000, 0) == POLL && h.phase == nbt::gui::HoldStarting);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (nbt::gui::HoldAside | POLL));

    /* Entry selected while a GUI already runs (opened before the HMI restarted). */
    h.reset();
    CHECK(h.entry(now, true) == (PARENT | POLL) && h.phase == nbt::gui::HoldOpen);
    reach_blank(&h, 1);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (SHOW | POLL));
}

static void test_signal_file(void) {
    static const char *bad[] = {"",
                                "aside\n",
                                " aside\n",
                                "1 aside",
                                "1 aside\n\n",
                                "1  aside\n",
                                "01 aside\n",
                                "1 Aside\n",
                                "1 hide\n",
                                "-1 show\n",
                                "1 show",
                                "18446744073709551616 show\n",
                                "1x show\n",
                                "123456789012345678901 show\n",
                                "1 show\nmore\n"};
    char path[] = "/tmp/nbt-hold-XXXXXX";
    uint64_t sequence = 0;
    nbt::gui::HoldSignal signal = nbt::gui::SignalShow;
    size_t i;
    CHECK(nbt::gui::holdSignalParse("0 aside\n", 8, sequence, signal) == 0 && sequence == 0 &&
          signal == nbt::gui::SignalAside);
    CHECK(nbt::gui::holdSignalParse("18446744073709551615 show\n", 26, sequence, signal) == 0 &&
          sequence == UINT64_MAX && signal == nbt::gui::SignalShow);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(nbt::gui::holdSignalParse(bad[i], strlen(bad[i]), sequence, signal) == -1);
    }
    {
        int fd = mkstemp(path);
        CHECK(fd >= 0 && close(fd) == 0 && unlink(path) == 0);
    }
    CHECK(nbt::gui::holdSignalRead(path, sequence, signal) == 1);
    CHECK(nbt::gui::holdSignalWrite(path, 42, nbt::gui::SignalAside) == 0);
    CHECK(nbt::gui::holdSignalRead(path, sequence, signal) == 0 && sequence == 42 &&
          signal == nbt::gui::SignalAside);
    CHECK(nbt::gui::holdSignalWrite(path, 43, nbt::gui::SignalShow) == 0);
    CHECK(nbt::gui::holdSignalRead(path, sequence, signal) == 0 && sequence == 43 &&
          signal == nbt::gui::SignalShow);
    {
        FILE *file = fopen(path, "w");
        CHECK(file && fputs("", file) >= 0 && fclose(file) == 0);
    }
    CHECK(nbt::gui::holdSignalRead(path, sequence, signal) == -1); /* truncated mid-write */
    {
        FILE *file = fopen(path, "w");
        CHECK(file);
        for (i = 0; i < 100; ++i) {
            fputc('1', file);
        }
        CHECK(fclose(file) == 0);
    }
    CHECK(nbt::gui::holdSignalRead(path, sequence, signal) == -1);
    CHECK(unlink(path) == 0);
    CHECK(nbt::gui::holdSignalRead("/tmp", sequence, signal) == -1); /* not a regular file */
}

static void test_liveness(void) {
    char path[] = "/tmp/nbt-live-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0 && close(fd) == 0 && unlink(path) == 0);
    CHECK(nbt::gui::holdLivenessWrite(path, getpid(), now) == 0);
    CHECK(nbt::gui::holdLivenessRead(path, now));
    CHECK(nbt::gui::holdLivenessRead(path, now + nbt::gui::kHoldLivenessMs));
    CHECK(!nbt::gui::holdLivenessRead(path, now + nbt::gui::kHoldLivenessMs + 1));
    CHECK(!nbt::gui::holdLivenessRead(path, now - 1));
    CHECK(nbt::gui::holdLivenessWrite(path, getpid(), now + 5000) == 0);
    CHECK(!nbt::gui::holdLivenessRead(path, now));
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        _exit(0);
    }
    CHECK(waitpid(child, NULL, 0) == child);
    CHECK(nbt::gui::holdLivenessWrite(path, child, now) == 0);
    CHECK(!nbt::gui::holdLivenessRead(path, now));
    CHECK(unlink(path) == 0);
    CHECK(!nbt::gui::holdLivenessRead(path, now));
    CHECK(nbt::gui::holdLivenessWrite(path, 0, now) == -1);
}

static void test_shmem_hardening(void) {
    char base[] = "/tmp/nbt-sec-XXXXXX";
    char victim[] = "/tmp/nbt-victim-XXXXXX";
    int fd = mkstemp(base);
    int vfd = mkstemp(victim);
    CHECK(fd >= 0 && vfd >= 0 && close(fd) == 0 && close(vfd) == 0 && unlink(base) == 0);
    CHECK(nbt::gui::holdSignalWrite(base, 10, nbt::gui::SignalShow) == 0);
    struct stat st;
    CHECK(lstat(base, &st) == 0 && (st.st_mode & 0777) == 0600);
    CHECK(chmod(base, 0602) == 0);
    uint64_t seq = 0;
    nbt::gui::HoldSignal sig = nbt::gui::SignalAside;
    CHECK(nbt::gui::holdSignalRead(base, seq, sig) == nbt::gui::HoldReadError);
    CHECK(nbt::gui::holdSignalWrite(base, 11, nbt::gui::SignalAside) == -1);
    CHECK(unlink(base) == 0);
    CHECK(symlink(victim, base) == 0);
    CHECK(nbt::gui::holdSignalWrite(base, 12, nbt::gui::SignalShow) == -1);
    CHECK(nbt::gui::holdSignalRead(base, seq, sig) == nbt::gui::HoldReadError);
    CHECK(unlink(base) == 0 && unlink(victim) == 0);
}

int main(void) {
    test_back_is_held_and_exit_releases();
    test_hard_key_steps_aside();
    test_refused_focus_and_failed_start();
    test_signal_file();
    test_liveness();
    test_shmem_hardening();
    return 0;
}
