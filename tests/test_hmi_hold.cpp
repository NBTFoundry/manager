// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/car_data_relay.hpp"
#include "nbt/gui/hmi_hold.hpp"
#include "nbt/gui/shortcut_record.hpp"

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
    /* iDrive ignores the parent event (a popup is open, or after a start its
     * restored menu): retry from the entry's list, then give up. Never held,
     * the GUI is never told to show. */
    CHECK(h.entry(now, false) == (nbt::gui::HoldSpawn | PARENT | POLL));
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == POLL);
    CHECK(poll_after(&h, 1000, 1) == (nbt::gui::HoldReopen | POLL));
    CHECK(poll_after(&h, 1000, 1) == (nbt::gui::HoldReopen | POLL));
    CHECK(poll_after(&h, 1000, 1) == (nbt::gui::HoldReopen | POLL));
    CHECK(h.tries == nbt::gui::kHoldTries);
    CHECK(poll_after(&h, 1000, 1) == POLL && poll_after(&h, 5000, 1) == POLL);
    /* The reopen worked: the parent opens, the blank state follows, shown. */
    h.reset();
    CHECK(h.entry(now, true) == (PARENT | POLL));
    CHECK(poll_after(&h, 1000, 1) == (nbt::gui::HoldReopen | POLL));
    h.focus(nbt::gui::HoldParent, 1, now);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (FOCUS | POLL));
    h.focus(nbt::gui::HoldBlank, 1, now);
    CHECK(poll_after(&h, nbt::gui::kHoldPollMs, 1) == (SHOW | POLL));
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

static nbt::gui::AppRequest request(const char *app, uint64_t sequence, nbt_hold_word word) {
    nbt::gui::AppRequest r;
    memset(&r, 0, sizeof(r));
    strcpy(r.app, app);
    r.sequence = sequence;
    r.word = word;
    return r;
}

/* An application's hold: held on the blank state, kept through BACK, peek
 * enforces nothing, iDrive taking the screen says aside (once), a new
 * request holds again, release and a stale request release, the manager and
 * another application take it over. */
static void test_application_hold(void) {
    nbt::gui::AppHold a;
    char who[NBT_HOLD_APP_MAX + 1];
    nbt::gui::AppAnswer answer;
    nbt::gui::AppRequest aa = request("android-auto", 7, NBT_HOLD_HOLD);
    /* No request, or a peek alone: nothing. */
    CHECK(a.poll(now, NULL, false, who, answer) == 0 && answer == nbt::gui::AnswerNone);
    nbt::gui::AppRequest peek = request("android-auto", 7, NBT_HOLD_PEEK);
    CHECK(a.poll(now, &peek, false, who, answer) == 0 && !a.active());
    /* hold: the parent first, then the blank state; held once iDrive is there. */
    CHECK(a.poll(now, &aa, false, who, answer) == (PARENT | POLL) && a.active());
    a.focus(nbt::gui::HoldParent, 1, now + 30);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, false, who, answer) == (FOCUS | POLL));
    a.focus(nbt::gui::HoldParent, 0, now + 30);
    a.focus(nbt::gui::HoldBlank, 1, now + 40);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, false, who, answer) == POLL && answer == nbt::gui::AnswerHeld &&
          !strcmp(who, "android-auto"));
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, false, who, answer) == POLL && answer == nbt::gui::AnswerNone);
    /* BACK: iDrive goes to the parent, the hold comes back at once. */
    a.focus(nbt::gui::HoldBlank, 0, now + 10);
    a.focus(nbt::gui::HoldParent, 1, now + 20);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, false, who, answer) == (FOCUS | POLL) && answer == nbt::gui::AnswerNone);
    a.focus(nbt::gui::HoldParent, 0, now + 30);
    a.focus(nbt::gui::HoldBlank, 1, now + 40);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, false, who, answer) == POLL);
    /* peek: the favourites bar; iDrive leaving meanwhile is not enforced nor aside. */
    peek.sequence = 8;
    a.focus(nbt::gui::HoldBlank, 0, now + 10);
    for (int i = 0; i < 8; ++i) {
        now += nbt::gui::kHoldPollMs;
        CHECK(a.poll(now, &peek, false, who, answer) == POLL && answer == nbt::gui::AnswerNone);
    }
    a.focus(nbt::gui::HoldBlank, 1, now + 10);
    /* MENU: iDrive leaves for its own screen: aside, and no re-hold. */
    aa.sequence = 9;
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, false, who, answer) == POLL);
    a.focus(nbt::gui::HoldBlank, 0, now + 10);
    unsigned polls = 0;
    do {
        now += nbt::gui::kHoldPollMs;
        CHECK(a.poll(now, &aa, false, who, answer) == POLL && ++polls <= 3);
    } while (answer == nbt::gui::AnswerNone);
    CHECK(answer == nbt::gui::AnswerAside && !a.active() && polls >= 2);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, false, who, answer) == 0 && answer == nbt::gui::AnswerNone);
    /* A new request holds again; release releases. */
    aa.sequence = 10;
    CHECK(a.poll(now, &aa, false, who, answer) == (PARENT | POLL) && a.active());
    a.focus(nbt::gui::HoldParent, 1, now + 10);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, false, who, answer) == (FOCUS | POLL));
    nbt::gui::AppRequest release = request("android-auto", 11, NBT_HOLD_RELEASE);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &release, false, who, answer) == nbt::gui::HoldRelease && !a.active());
    a.focus(nbt::gui::HoldParent, 0, now);
    /* A stale request (its host ended): released, without an answer. */
    aa.sequence = 12;
    CHECK(a.poll(now, &aa, false, who, answer) == (PARENT | POLL));
    a.focus(nbt::gui::HoldParent, 1, now + 10);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, NULL, false, who, answer) == nbt::gui::HoldRelease &&
          answer == nbt::gui::AnswerNone);
    a.focus(nbt::gui::HoldParent, 0, now);
    /* The manager's window takes it: aside, no release event. */
    aa.sequence = 13;
    CHECK(a.poll(now, &aa, false, who, answer) == (PARENT | POLL));
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &aa, true, who, answer) == 0 && answer == nbt::gui::AnswerAside &&
          !a.active());
    CHECK(a.poll(now, &aa, false, who, answer) == 0); /* told aside: waits for a new request */
    /* Another application takes it: aside for the first, hold for the second. */
    aa.sequence = 14;
    CHECK(a.poll(now, &aa, false, who, answer) == (PARENT | POLL));
    nbt::gui::AppRequest other = request("demo", 1, NBT_HOLD_HOLD);
    now += nbt::gui::kHoldPollMs;
    CHECK(a.poll(now, &other, false, who, answer) == (PARENT | POLL) &&
          answer == nbt::gui::AnswerAside && !strcmp(who, "android-auto") && a.active() &&
          !strcmp(a.app(), "demo"));
}

/* The wire lines and files of bridge 1.6. */
static void test_bridge_files(void) {
    char line[NBT_HOLD_LINE_MAX + 1], app[NBT_HOLD_APP_MAX + 1], value[NBT_CDS_VALUE_MAX + 1];
    uint64_t ms = 0, seq = 0;
    nbt_hold_word word;
    nbt_hold_state state;
    CHECK(nbt_hold_request_format(42, NBT_HOLD_PEEK, line, sizeof(line)) == 8 &&
          !strcmp(line, "42 peek\n"));
    CHECK(!nbt_hold_request_parse(line, 8, &seq, &word) && seq == 42 && word == NBT_HOLD_PEEK);
    const char *badRequests[] = {"42 peek",
                                 "042 hold\n",
                                 "42 hold \n",
                                 "42  hold\n",
                                 "hold\n",
                                 "42 held\n",
                                 "18446744073709551616 hold\n"};
    for (unsigned i = 0; i < sizeof(badRequests) / sizeof(badRequests[0]); ++i) {
        CHECK(nbt_hold_request_parse(badRequests[i], strlen(badRequests[i]), &seq, &word));
    }
    CHECK(nbt_hold_request_parse("1 hold\n\0", 8, &seq, &word));
    CHECK(nbt_hold_relay_format(1000, 7, "android-auto", NBT_HOLD_HOLD, line, sizeof(line)) > 0 &&
          !strcmp(line, "1000 7 android-auto hold\n"));
    CHECK(!nbt_hold_relay_parse(line, strlen(line), &ms, &seq, app, &word) && ms == 1000 &&
          seq == 7 && !strcmp(app, "android-auto") && word == NBT_HOLD_HOLD);
    CHECK(nbt_hold_relay_format(1, 1, "../x", NBT_HOLD_HOLD, line, sizeof(line)) < 0);
    CHECK(nbt_hold_relay_parse("1 1 a/b hold\n", 13, &ms, &seq, app, &word));
    CHECK(nbt_hold_state_format(3, NBT_HOLD_ASIDE, line, sizeof(line)) > 0 &&
          !strcmp(line, "3 aside\n") && !nbt_hold_state_parse(line, 8, &seq, &state) &&
          state == NBT_HOLD_ASIDE);
    CHECK(nbt_cds_value_format(5, "{\"gear\":2}", line, sizeof(line)) > 0 &&
          !nbt_cds_value_parse(line, strlen(line), &seq, value) && seq == 5 &&
          !strcmp(value, "{\"gear\":2}"));
    CHECK(nbt_cds_value_parse("5 a\tb\n", 6, &seq, value) &&
          nbt_cds_value_format(1, "a\nb", line, sizeof(line)) < 0);
    unsigned short utf16[] = {'{', '"', 'a', '"', ':', '1', '}', 0}, wide[] = {'a', 0x263a, 0};
    CHECK(nbt::gui::carDataAscii(utf16, value) && !strcmp(value, "{\"a\":1}") &&
          !nbt::gui::carDataAscii(wide, value));

    /* Files in a private directory: relay, answer, records, freshness. */
    char dir[] = "/tmp/nbt-hold-files-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char relay[128], names[NBT_CDS_POLICY_MAX + 1], path[160], text[NBT_NET_POLICY_MAX + 1];
    snprintf(relay, sizeof(relay), "%s/nbt-bridge1-hmi", dir);
    nbt::gui::AppRequest r;
    CHECK(!nbt::gui::appHoldRelayWrite(relay, 10000, 3, "android-auto", NBT_HOLD_HOLD));
    CHECK(nbt::gui::appHoldRelayRead(relay, 10000 + NBT_HOLD_FRESH_MS, r) && r.sequence == 3 &&
          !strcmp(r.app, "android-auto") && r.word == NBT_HOLD_HOLD);
    CHECK(!nbt::gui::appHoldRelayRead(relay, 10001 + NBT_HOLD_FRESH_MS, r)); /* stale */
    CHECK(!nbt::gui::appHoldRelayRead(relay, 9999, r));                      /* from the future */
    CHECK(!nbt::gui::appHoldAnswerWrite(dir, "android-auto", 4, nbt::gui::AnswerHeld));
    CHECK(!nbt::gui::appHoldAnswerPath(dir, "android-auto", path, sizeof(path)) &&
          nbt::gui::shmemReadLine(path, line, sizeof(line)) == 7 && !memcmp(line, "4 held\n", 7));
    CHECK(nbt::gui::appHoldAnswerWrite(dir, "a/b", 4, nbt::gui::AnswerHeld) == -1);
    CHECK(
        !nbt::gui::carDataWantWrite(dir, "android-auto", 20000, "driving.gear,sensors.PDCStatus"));
    CHECK(nbt::gui::carDataWantWrite(dir, "android-auto", 20000, "gear") == -1);
    CHECK(!nbt::gui::carDataWantPath(dir, "android-auto", path, sizeof(path)) &&
          nbt::gui::carDataWantRead(path, 21000, names) &&
          !strcmp(names, "driving.gear,sensors.PDCStatus") &&
          !nbt::gui::carDataWantRead(path, 20001 + NBT_HOLD_FRESH_MS, names));
    CHECK(nbt::gui::carDataWantEntry("nbt-bridge1-android-auto.want") &&
          !nbt::gui::carDataWantEntry("nbt-bridge1-cds-driving.gear") &&
          !nbt::gui::carDataWantEntry("nbt-bridge1-a.b.want") &&
          !nbt::gui::carDataWantEntry(".want"));
    CHECK(!nbt::gui::carDataValuePath(dir, "driving.gear", path, sizeof(path)) &&
          strstr(path, "/nbt-bridge1-cds-driving.gear") &&
          nbt::gui::carDataValuePath(dir, "../x", path, sizeof(path)) == -1);
    CHECK(!nbt::gui::netWfd0InUse(dir, 30000));
    CHECK(!nbt::gui::netRecordWrite(dir, "android-auto", 30000, "tcp:5277@uap0"));
    CHECK(!nbt::gui::netWfd0InUse(dir, 30000)); /* uap0 only */
    CHECK(!nbt::gui::netRecordWrite(dir, "demo", 30000, "tcp:5277@uap0+wfd0"));
    CHECK(nbt::gui::netWfd0InUse(dir, 30500) &&
          !nbt::gui::netWfd0InUse(dir, 30001 + NBT_HOLD_FRESH_MS));
    CHECK(!nbt::gui::appRecordPath(dir, "demo", "net", path, sizeof(path)) &&
          nbt::gui::appRecordRead(path, 30000, text, sizeof(text)) &&
          !strcmp(text, "tcp:5277@uap0+wfd0"));
    char command[64];
    snprintf(command, sizeof(command), "rm -rf %s", dir);
    CHECK(system(command) == 0);
}

/* The App shortcut record (shortcut_record.hpp): parsed strictly, due once. */
static void test_shortcut_record(void) {
    nbt::gui::ShortcutRecord r;
    CHECK(nbt::gui::shortcutRecordParse("12 open\n", 8, r) && r.word == nbt::gui::ShortcutOpen &&
          r.ms == 12 && !r.app[0]);
    CHECK(nbt::gui::shortcutRecordParse("5 start android-auto\n", 21, r) &&
          r.word == nbt::gui::ShortcutStart && !strcmp(r.app, "android-auto"));
    CHECK(nbt::gui::shortcutRecordParse("5 taken\n", 8, r) && r.word == nbt::gui::ShortcutTaken);
    const char *bad[] = {"12 open",         "012 open\n",   "12 opened\n", "12 start\n",
                         "12 start ../x\n", "12 start A\n", "x open\n",    "12  open\n"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(!nbt::gui::shortcutRecordParse(bad[i], strlen(bad[i]), r));
    }
    char text[96];
    r.word = nbt::gui::ShortcutStart;
    r.ms = 7;
    strcpy(r.app, "pcsx");
    CHECK(nbt::gui::shortcutRecordFormat(r, text, sizeof(text)) == 13 &&
          !strcmp(text, "7 start pcsx\n"));
    CHECK(nbt::gui::shortcutRecordDue(r, 7, 7 + nbt::gui::kShortcutFreshMs) &&
          !nbt::gui::shortcutRecordDue(r, 8, 9) && /* before the press */
          !nbt::gui::shortcutRecordDue(r, 0, 8 + nbt::gui::kShortcutFreshMs));
    r.word = nbt::gui::ShortcutTaken;
    CHECK(!nbt::gui::shortcutRecordDue(r, 0, 8));
    strcpy(r.app, "../x");
    r.word = nbt::gui::ShortcutStart;
    CHECK(nbt::gui::shortcutRecordFormat(r, text, sizeof(text)) == -1);
}

int main(void) {
    test_shortcut_record();
    test_back_is_held_and_exit_releases();
    test_hard_key_steps_aside();
    test_refused_focus_and_failed_start();
    test_signal_file();
    test_liveness();
    test_shmem_hardening();
    test_application_hold();
    test_bridge_files();
    return 0;
}
