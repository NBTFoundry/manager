// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/selftest_ui.hpp"

#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/log.h"
#include "nbt/util.h"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

typedef SelfTestState S;
// How long an application may take: to be seen running, then in all.
const uint64_t kSeenMs = 15000, kRunMs = 90000, kStopMs = 20000;
const char kInteractive[] = "manager-selftest"; // the module (nbt/bundled.h)

void phase(UiController &ui, S::Phase next) {
    ui.selfTest.phase = next;
    ui.selfTest.phaseMs = ui.clockMs;
}

/* "SELFTEST\tready\t<app>,<app>": the applications to run, all bundled. */
/* The next application still running is stopped (it would hold the screen),
 * as holding its row would; then the preparation. */
void stopNext(UiController &ui) {
    for (size_t i = 0; i < ui.appCount; ++i) {
        if (ui.running[i]) {
            snprintf(ui.target, sizeof(ui.target), "%s", ui.apps[i]);
            nbt_logf("self test: stopping %s first", ui.target);
            ui.faultStop = 0;
            phase(ui, S::StopOthers);
            ui.queued = R_STOP;
            return;
        }
    }
    phase(ui, S::Preparing);
    ui.queued = R_SELFTEST_PREPARE;
}

void start(UiController &ui) {
    ui.message[0] = 0;
    ui.selfTest.saw = ui.selfTest.heard = '-';
    ui.selfTest.instructed = false;
    ui.go(SelfTestRun);
    stopNext(ui);
}

/* The answer to the question shown, then the next question or the report. */
void answer(UiController &ui, char said) {
    S &t = ui.selfTest;
    if (t.phase == S::AskScreen) {
        t.saw = said;
        phase(ui, S::AskSound);
        ui.focus = 0;
        return;
    }
    t.heard = said;
    ui.go(SelfTestRun);
    phase(ui, S::Reporting);
    ui.queued = R_SELFTEST_REPORT;
}

} // namespace

bool selfTestScreen(Screen screen) {
    return screen == SelfTest || screen == SelfTestRun || screen == SelfTestAsk ||
           screen == SelfTestResults;
}

size_t selfTestItemCount(const UiController &ui) {
    const S &t = ui.selfTest;
    switch (ui.screen) {
    case SelfTest:
        return t.have ? 2 : 1;
    case SelfTestRun:
        return t.phase == S::Instruction ? 1 : 0;
    case SelfTestAsk:
        return 2;
    case SelfTestResults:
        return t.rows + 1;
    default:
        return 0;
    }
}

bool selfTestPress(UiController &ui, Key key) {
    S &t = ui.selfTest;
    switch (ui.screen) {
    case Settings:
        if (!ui.managerSettings() || key != KeySelect ||
            ui.focus != ui.managerEntry(EntrySelfTest)) {
            return false;
        }
        ui.message[0] = 0;
        ui.go(SelfTest);
        phase(ui, S::Idle);
        ui.queued = R_SELFTEST_LAST;
        return true;
    case SelfTest:
        if (key == KeyBack) {
            ui.message[0] = 0;
            ui.go(Settings);
            ui.focus = ui.managerEntry(EntrySelfTest);
        } else if (key == KeySelect && ui.focus == 0) {
            start(ui);
        } else if (key == KeySelect && t.have) {
            ui.go(SelfTestResults);
        }
        return true;
    case SelfTestRun:
        if (key == KeySelect && t.phase == S::Instruction) {
            t.instructed = true;
            phase(ui, S::Next);
        }
        return true; // BACK waits: an application is being prepared or run
    case SelfTestAsk:
        if (key == KeySelect || key == KeyBack) {
            answer(ui, key == KeyBack ? '-' : ui.focus == 0 ? 'y' : 'n');
        }
        return true;
    case SelfTestResults:
        if (key == KeyBack || (key == KeySelect && ui.focus == t.rows)) {
            ui.go(SelfTest);
        }
        return true;
    default:
        return false;
    }
}

bool selfTestRequest(const UiController &ui, int kind, Request &request) {
    const S &t = ui.selfTest;
    if (kind == R_SELFTEST_PREPARE) {
        request.timeoutMs = 60000; // a release verified, packages copied and installed
        snprintf(request.command, sizeof(request.command), "SELFTEST PREPARE");
    } else if (kind == R_SELFTEST_REPORT) {
        snprintf(request.command, sizeof(request.command), "SELFTEST REPORT %c %c", t.saw, t.heard);
    } else if (kind == R_SELFTEST_LAST) {
        snprintf(request.command, sizeof(request.command), "SELFTEST LAST");
    } else {
        return false;
    }
    return true;
}

int selfTestReply(UiController &ui, int kind, const char *reply) {
    if (kind == R_SELFTEST_LAST) {
        if (strcmp(reply, "SELFTEST\tnone") && !ReplyParser::isError(reply) &&
            !selfTestParseReport(ui.selfTest, reply)) {
            return -1;
        }
        return 0;
    }
    if (ReplyParser::isError(reply)) {
        ui.go(SelfTest);
        phase(ui, S::Idle);
        textf(ui.message, sizeof(ui.message), !strcmp(reply, "ERR busy") ? T_ST_BUSY : T_ST_CANNOT);
        return 0;
    }
    if (kind == R_SELFTEST_PREPARE) {
        if (!selfTestParseReady(ui.selfTest, reply)) {
            return -1;
        }
        phase(ui, S::Next);
        return 0;
    }
    if (!selfTestParseReport(ui.selfTest, reply)) {
        return -1;
    }
    phase(ui, S::Idle);
    ui.go(SelfTestResults);
    return 0;
}

bool selfTestPoll(UiController &ui, int &kind) {
    S &t = ui.selfTest;
    if (ui.screen != SelfTestRun) {
        return false;
    }
    const uint64_t elapsed = ui.clockMs - t.phaseMs;
    if (t.phase == S::Running &&
        elapsed >= (ui.appRun == 1 ? kSeenMs : kRunMs)) { // never seen, or never ends
        nbt_logf("self test: %s did not end; stopping it", ui.target);
        (void)selfTestAbort(ui);
        return false; // STOP is queued
    }
    if (t.phase == S::Stopping && elapsed >= kStopMs) {
        ui.stopping = 0;
        phase(ui, S::Next);
    }
    if (t.phase == S::StopOthers && elapsed >= kStopMs) { // it does not stop: go on anyway
        nbt_logf("self test: %s did not stop", ui.target);
        ui.stopping = 0;
        phase(ui, S::Preparing);
        kind = R_SELFTEST_PREPARE;
        return true;
    }
    if (t.phase == S::Ended && !ui.appRun) { // the panel is the manager's again
        phase(ui, S::Next);
    }
    if (t.phase == S::Running || t.phase == S::Stopping || t.phase == S::StopOthers) {
        kind = R_RUNNING;
        return true;
    }
    if (t.phase != S::Next) {
        return false;
    }
    if (t.next == t.count) {
        ui.go(SelfTestAsk);
        phase(ui, t.count ? S::AskScreen : S::AskSound);
        if (!t.count) { // nothing ran: nothing to ask
            answer(ui, '-');
        }
        return false;
    }
    if (!strcmp(t.apps[t.next], kInteractive) && !t.instructed) {
        phase(ui, S::Instruction);
        ui.focus = 0;
        return false;
    }
    snprintf(ui.target, sizeof(ui.target), "%s", t.apps[t.next++]);
    phase(ui, S::Starting);
    kind = R_START;
    return true;
}

bool selfTestStarted(UiController &ui, const char *reply) {
    S &t = ui.selfTest;
    if (t.phase != S::Starting) {
        return false;
    }
    if (!strcmp(reply, "OK screen")) {
        ui.handover = 1; // the GUI hands it the panel (appRun 1)
        phase(ui, S::Running);
    } else if (!strcmp(reply, "OK")) {
        ui.appRun = 1; // it runs without the panel: followed in RUNNING all the same
        phase(ui, S::Running);
    } else {
        nbt_logf("self test: %s not started: %s", ui.target, reply);
        phase(ui, S::Next); // its report says it did not run
    }
    return true;
}

void selfTestRunning(UiController &ui) {
    S &t = ui.selfTest;
    if (t.phase == S::Running && ui.appRun == 3) {
        phase(ui, S::Ended); // GuiSession takes the panel back (takeAppEnded)
    } else if (t.phase == S::Stopping && !ui.targetRunning) {
        ui.stopping = 0;
        phase(ui, S::Next);
    } else if (t.phase == S::StopOthers && !ui.targetRunning) {
        ui.stopping = 0;
        stopNext(ui);
    }
}

bool selfTestAbort(UiController &ui) {
    S &t = ui.selfTest;
    if (ui.screen != SelfTestRun || (t.phase != S::Running && t.phase != S::Starting)) {
        return false;
    }
    ui.appRun = 3; // GuiSession takes the panel back if the application still has it
    ui.handover = 0;
    ui.faultStop = 0; // the manager's own test application: no fault counted
    ui.queued = R_STOP;
    phase(ui, S::Stopping);
    return true;
}

} // namespace gui
} // namespace nbt
