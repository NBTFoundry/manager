// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_HMI_HOLD_HPP
#define NBT_GUI_HMI_HOLD_HPP

#include "nbt/bridge_hold.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace gui {

static const char kHoldSignalPath[] = "/dev/shmem/nbt-manager-hmi";
static const char kHoldLivenessPath[] = "/dev/shmem/nbt-manager-gui-live";
static const unsigned kHoldPollMs = 250;
static const unsigned kHoldStartMs = 10000;
static const unsigned kHoldTries = 4;
static const unsigned kHoldLivenessMs = 1500;

enum HoldPhase { HoldIdle, HoldStarting, HoldOpen, HoldSteppedAside };

enum HoldState { HoldBlank, HoldParent };

enum HoldSignal { SignalAside = 1, SignalShow = 2 };

enum HoldReadResult { HoldReadError = -1, HoldReadOk = 0, HoldReadAbsent = 1 };

enum HoldAction {
    HoldSpawn = 1u,
    HoldFocus = 2u,
    HoldRelease = 4u,
    HoldAside = 8u,
    HoldShow = 16u,
    HoldPoll = 32u,
    HoldToParent = 64u // put iDrive on the parent state 15 first
};

class HmiHold {
public:
    HmiHold();
    void reset();
    unsigned entry(uint64_t nowMs, bool running);
    void focus(HoldState state, bool focused, uint64_t nowMs);
    unsigned poll(uint64_t nowMs, bool running);

    HoldPhase phase;
    uint64_t startedMs;
    uint64_t focusMs;
    uint64_t leftMs;
    uint64_t parentMs;
    unsigned tries;
    bool blank;
    bool parent;
    bool seen;
    bool shown; // HoldShow sent since the entry: the GUI may cover iDrive
};

/* An application's focus hold (capability `hold`, bridge 1.6): its host relays
 * the application's request in kAppHoldRelayPath; the plugin answers in
 * <kAppHoldAnswerDir>/nbt-bridge1-<app>.hmi (include/nbt/bridge_hold.h). */
static const char kAppHoldRelayPath[] = "/dev/shmem/nbt-bridge1-hmi";
static const char kAppHoldAnswerDir[] = "/dev/shmem";

enum AppAnswer { AnswerNone = 0, AnswerHeld = 1, AnswerAside = 2 };

struct AppRequest {
    uint64_t sequence;
    char app[NBT_HOLD_APP_MAX + 1];
    nbt_hold_word word;
};

/* The decisions, host-testable: poll() is given the relay's request when it is
 * fresh (else NULL) and whether the manager's own hold is in use, which has
 * priority. It returns HoldAction bits for the focus events and says what to
 * tell which application. `hold` takes iDrive to the blank state as the
 * entry does and keeps it there after BACK; `peek` keeps it without
 * re-asserting it (iDrive's favourites preview); iDrive leaving for its own
 * screen, the manager or another application taking the hold, answer `aside`,
 * after which only a new request (a new sequence) holds again. */
class AppHold {
public:
    AppHold();
    void reset();
    unsigned poll(uint64_t nowMs, const AppRequest *request, bool manager,
                  char answerApp[NBT_HOLD_APP_MAX + 1], AppAnswer &answer);
    void focus(HoldState state, bool focused, uint64_t nowMs);
    bool active() const {
        return active_;
    }
    const char *app() const {
        return app_;
    }

private:
    unsigned stepAside(char answerApp[NBT_HOLD_APP_MAX + 1], AppAnswer &answer);

    HmiHold hold_;
    char app_[NBT_HOLD_APP_MAX + 1];
    uint64_t sequence_;
    bool active_;
    bool asideKnown_;
    uint64_t asideSequence_;
    char asideApp_[NBT_HOLD_APP_MAX + 1];
};

/* The relay's request, when its line parses and is at most NBT_HOLD_FRESH_MS
 * old by nowMs (the hosts and the plugin share the monotonic clock). */
bool appHoldRelayRead(const char *path, uint64_t nowMs, AppRequest &out);
int appHoldRelayWrite(const char *path, uint64_t nowMs, uint64_t sequence, const char *app,
                      nbt_hold_word word);
/* <dir>/nbt-bridge1-<app>.hmi, `<sequence> held|aside`. */
int appHoldAnswerPath(const char *dir, const char *app, char *out, size_t size);
int appHoldAnswerWrite(const char *dir, const char *app, uint64_t sequence, AppAnswer answer);

/* A line in /dev/shmem, rewritten in place with one write and cut to its
 * length; read whole. Private regular files of this user only. */
int shmemWriteLine(const char *path, const char *text, size_t length);
/* Bytes read (at most `size`), or -1. */
int shmemReadLine(const char *path, char *out, size_t size);

int holdSignalParse(const char *text, size_t size, uint64_t &sequence, HoldSignal &signal);
HoldReadResult holdSignalRead(const char *path, uint64_t &sequence, HoldSignal &signal);
int holdSignalWrite(const char *path, uint64_t sequence, HoldSignal signal);
int holdLivenessWrite(const char *path, pid_t pid, uint64_t nowMs);
bool holdLivenessRead(const char *path, uint64_t nowMs);

} // namespace gui
} // namespace nbt

#endif
