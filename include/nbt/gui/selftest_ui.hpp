// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_SELFTEST_UI_HPP
#define NBT_GUI_SELFTEST_UI_HPP

/* Settings > Self test (src/gui/selftest_ui.cpp, docs/self-test.md): the
 * worker installs the bundled applications (SELFTEST PREPARE), the GUI starts
 * each as START does and hands it the panel, asks the driver about the colour
 * bars and the beeps, then shows what did not pass (SELFTEST REPORT; the kept
 * report with SELFTEST LAST). */

#include "nbt/bundled.h"
#include "nbt/gui/ui_view.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace gui {

class UiController;
struct Request;

struct SelfTestState {
    enum Phase {
        Idle,
        Preparing,   // SELFTEST PREPARE sent
        Next,        // the next application, or the questions
        Instruction, // before the module: what the driver will see and do
        Starting,    // START sent
        Running,     // on the panel (or running without it)
        Ended,       // RUNNING no longer lists it: once the panel is back, Next
        Stopping,    // stopped after it did not appear or ran too long
        AskScreen,
        AskSound,
        Reporting // SELFTEST REPORT sent
    };
    static const size_t kRows = 16;
    Phase phase;
    uint64_t phaseMs; // when the phase began
    char apps[NBT_BUNDLED_COUNT][kIdMax + 1];
    size_t count, next;
    bool instructed; // the module's instruction was shown
    char saw, heard; // 'y', 'n' or '-'
    // The last report: counts and what did not pass.
    bool have;
    unsigned passed, failed, skipped;
    char keys[kRows][24];
    char codes[kRows];
    char details[kRows][41];
    size_t rows;
};

/* SELFTEST PREPARE's "SELFTEST\tready\t<app>,..." (bundled ids only) and
 * REPORT's or LAST's counts and rows (selftest_reply.cpp); false when malformed. */
bool selfTestParseReady(SelfTestState &state, const char *reply);
bool selfTestParseReport(SelfTestState &state, const char *reply);
bool selfTestScreen(Screen screen);
size_t selfTestItemCount(const UiController &ui);
/* Keys for Settings' Self test entry and the self test's screens. */
bool selfTestPress(UiController &ui, Key key);
bool selfTestRequest(const UiController &ui, int kind, Request &request);
int selfTestReply(UiController &ui, int kind, const char *reply);
/* While the self test runs, what to ask the worker when nothing is queued:
 * RUNNING to follow the application, START for the next one. */
bool selfTestPoll(UiController &ui, int &kind);
/* START's reply for the self test's application; false when not its. */
bool selfTestStarted(UiController &ui, const char *reply);
/* After a RUNNING reply: follows the application on the panel. */
void selfTestRunning(UiController &ui);
/* abortApp while the self test runs it: stopped (no fault), then the next.
 * False when the self test does not run it. */
bool selfTestAbort(UiController &ui);
void selfTestView(const UiController &ui, UiView &view);

} // namespace gui
} // namespace nbt

#endif
