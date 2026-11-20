// SPDX-License-Identifier: MPL-2.0
// An application's hold (bridge 1.6, capability `hold`): include/nbt/gui/hmi_hold.hpp.
#include "nbt/gui/hmi_hold.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {

AppHold::AppHold()
    : hold_(), app_(), sequence_(0), active_(false), asideKnown_(false), asideSequence_(0),
      asideApp_() {
}

void AppHold::reset() {
    *this = AppHold();
}

void AppHold::focus(HoldState state, bool focused, uint64_t now) {
    hold_.focus(state, focused, now);
}

unsigned AppHold::stepAside(char answerApp[NBT_HOLD_APP_MAX + 1], AppAnswer &answer) {
    memcpy(answerApp, app_, sizeof(app_));
    answer = AnswerAside;
    asideKnown_ = true;
    asideSequence_ = sequence_;
    memcpy(asideApp_, app_, sizeof(app_));
    active_ = false;
    hold_.reset();
    return 0;
}

unsigned AppHold::poll(uint64_t now, const AppRequest *request, bool manager,
                       char answerApp[NBT_HOLD_APP_MAX + 1], AppAnswer &answer) {
    answer = AnswerNone;
    answerApp[0] = 0;
    const bool wants =
        request && (request->word == NBT_HOLD_HOLD || request->word == NBT_HOLD_PEEK);
    if (active_) {
        const bool same = request && !strcmp(request->app, app_);
        if (manager || (wants && !same)) {
            // The manager's window or another application takes iDrive: no
            // release event, the hold goes on for them.
            stepAside(answerApp, answer);
            if (manager) {
                return 0;
            }
        } else if (!wants) {
            // Released, or its host stopped refreshing the request (it ended).
            const unsigned actions = hold_.blank || hold_.parent ? HoldRelease : 0;
            active_ = false;
            hold_.reset();
            return actions;
        } else {
            sequence_ = request->sequence;
            if (request->word == NBT_HOLD_PEEK) {
                return HoldPoll; // iDrive's own bar is up: keep the state, enforce nothing
            }
            unsigned actions = hold_.poll(now, true);
            if (actions & HoldShow) {
                memcpy(answerApp, app_, sizeof(app_));
                answer = AnswerHeld;
            }
            if (actions & HoldAside) {
                stepAside(answerApp, answer);
                return HoldPoll;
            }
            return actions & ~static_cast<unsigned>(HoldShow);
        }
    }
    if (manager || !wants || request->word != NBT_HOLD_HOLD) {
        return 0;
    }
    if (asideKnown_ && asideSequence_ == request->sequence && !strcmp(asideApp_, request->app)) {
        return 0; // told aside: a new request holds again
    }
    active_ = true;
    asideKnown_ = false;
    memcpy(app_, request->app, sizeof(app_));
    sequence_ = request->sequence;
    hold_.reset();
    return hold_.entry(now, true) & ~static_cast<unsigned>(HoldSpawn);
}

bool appHoldRelayRead(const char *path, uint64_t now, AppRequest &out) {
    char text[NBT_HOLD_LINE_MAX + 1];
    const int count = shmemReadLine(path, text, sizeof(text));
    uint64_t ms = 0;
    return count > 0 &&
           !nbt_hold_relay_parse(text, static_cast<size_t>(count), &ms, &out.sequence, out.app,
                                 &out.word) &&
           now >= ms && now - ms <= NBT_HOLD_FRESH_MS;
}

int appHoldRelayWrite(const char *path, uint64_t now, uint64_t sequence, const char *app,
                      nbt_hold_word word) {
    char text[NBT_HOLD_LINE_MAX + 1];
    const int length = nbt_hold_relay_format(now, sequence, app, word, text, sizeof(text));
    return length < 0 || !path ? -1 : shmemWriteLine(path, text, static_cast<size_t>(length));
}

int appHoldAnswerPath(const char *dir, const char *app, char *out, size_t size) {
    char probe[NBT_HOLD_LINE_MAX + 1];
    // The name is checked as the relay checks it.
    if (!dir || !app || nbt_hold_relay_format(0, 0, app, NBT_HOLD_HOLD, probe, sizeof(probe)) < 0) {
        return -1;
    }
    const int n = snprintf(out, size, "%s/nbt-bridge1-%s.hmi", dir, app);
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : 0;
}

int appHoldAnswerWrite(const char *dir, const char *app, uint64_t sequence, AppAnswer answer) {
    char path[160], text[NBT_HOLD_LINE_MAX + 1];
    if (answer == AnswerNone || appHoldAnswerPath(dir, app, path, sizeof(path))) {
        return -1;
    }
    const int length = nbt_hold_state_format(
        sequence, answer == AnswerHeld ? NBT_HOLD_HELD : NBT_HOLD_ASIDE, text, sizeof(text));
    return length < 0 ? -1 : shmemWriteLine(path, text, static_cast<size_t>(length));
}

} // namespace gui
} // namespace nbt
