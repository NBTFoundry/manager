// SPDX-License-Identifier: MPL-2.0
/* The self test's screens (selftest_ui.hpp): its menu, the run, the two
 * questions and what did not pass, groups and checks named in the car's
 * language, details as the worker wrote them. */
#include "nbt/gui/selftest_ui.hpp"

#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/view_builder.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

typedef SelfTestState S;

TextId groupLabel(const char *key) {
    static const struct {
        const char *group;
        TextId id;
    } kGroups[] = {{"manager.", T_STG_MANAGER},
                   {"native.", T_STG_NATIVE},
                   {"guest.", T_STG_GUEST},
                   {"module.", T_STG_MODULE},
                   {"you.", T_STG_YOU}};
    for (size_t i = 0; i < sizeof(kGroups) / sizeof(kGroups[0]); ++i) {
        if (!strncmp(key, kGroups[i].group, strlen(kGroups[i].group))) {
            return kGroups[i].id;
        }
    }
    return kTextCount;
}

TextId checkLabel(const char *check) {
    static const struct {
        const char *name;
        TextId id;
    } kChecks[] = {
        {"release", T_STC_RELEASE},      {"runner", T_STC_RUNNER}, {"files", T_STC_FILES},
        {"space", T_STC_SPACE},          {"start", T_STC_START},   {"bridge", T_STC_BRIDGE},
        {"screen", T_STC_SCREEN},        {"sound", T_STC_SOUND},   {"status", T_STC_STATUS},
        {"cardata", T_STC_CARDATA},      {"diag", T_STC_DIAG},     {"phone", T_STC_PHONE},
        {"controller", T_STC_CONTROLLER}};
    for (size_t i = 0; i < sizeof(kChecks) / sizeof(kChecks[0]); ++i) {
        if (!strcmp(check, kChecks[i].name)) {
            return kChecks[i].id;
        }
    }
    return kTextCount;
}

void line(UiView &v, const char *text) {
    if (v.lineCount < kLines) {
        snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s", text);
    }
}

void item(UiView &v, const char *text, ItemStyle style) {
    if (v.itemCount < kItems) {
        snprintf(v.items[v.itemCount], sizeof(v.items[0]), "%s", text);
        v.itemStyle[v.itemCount++] = static_cast<unsigned char>(style);
    }
}

void resultsView(const S &t, UiView &v) {
    char out[kTextMax + 1], number[3][12], name[kTextMax + 1];
    snprintf(number[0], sizeof(number[0]), "%u", t.passed);
    snprintf(number[1], sizeof(number[1]), "%u", t.failed);
    snprintf(number[2], sizeof(number[2]), "%u", t.skipped);
    if (t.failed) {
        textf(out, sizeof(out), T_ST_SUMMARY, number[0], number[1]);
    } else {
        textf(out, sizeof(out), T_ST_ALL_PASSED, number[0]);
    }
    line(v, out);
    if (t.skipped) {
        textf(out, sizeof(out), T_ST_SKIPPED, number[2]);
        ViewBuilder::addWrapped(&v, out);
    }
    for (size_t i = 0; i < t.rows; ++i) {
        const char *dot = strchr(t.keys[i], '.');
        const TextId group = groupLabel(t.keys[i]), check = checkLabel(dot ? dot + 1 : "");
        if (group != kTextCount && check != kTextCount) {
            textf(name, sizeof(name), T_ST_JOIN, text(group), text(check));
        } else {
            snprintf(name, sizeof(name), "%s", t.keys[i]);
        }
        textf(out, sizeof(out), t.codes[i] == 'f' ? T_ST_FAILED_ROW : T_ST_SKIPPED_ROW, name,
              t.details[i]);
        item(v, out, ItemEntry);
    }
    item(v, text(T_OK), ItemAction);
}

} // namespace

void selfTestView(const UiController &ui, UiView &v) {
    const S &t = ui.selfTest;
    char out[kTextMax + 1], a[12], b[12];
    textf(v.title, sizeof(v.title), T_SELF_TEST);
    switch (ui.screen) {
    case SelfTest:
        ViewBuilder::addWrapped(&v, text(T_ST_INTRO));
        ViewBuilder::addWrapped(&v, text(T_ST_PARKED));
        if (t.have) {
            snprintf(a, sizeof(a), "%u", t.passed);
            snprintf(b, sizeof(b), "%u", t.failed);
            textf(out, sizeof(out), T_ST_LAST_SUMMARY, a, b);
            line(v, out);
        }
        if (ui.message[0]) {
            line(v, ui.message);
        }
        item(v, text(T_ST_START), ItemAction);
        if (t.have) {
            item(v, text(T_ST_LAST_RESULTS), ItemAction);
        }
        break;
    case SelfTestRun:
        if (t.phase == S::Instruction) {
            line(v, text(T_ST_NEXT));
            ViewBuilder::addWrapped(&v, text(T_ST_TURN));
            item(v, text(T_ST_CONTINUE), ItemAction);
            break;
        }
        v.busy = 1;
        if (t.phase == S::Preparing) {
            line(v, text(T_ST_PREPARING));
        } else if (t.phase == S::Reporting) {
            line(v, text(T_ST_REPORTING));
        } else {
            snprintf(a, sizeof(a), "%u", static_cast<unsigned>(t.next ? t.next : 1));
            snprintf(b, sizeof(b), "%u", static_cast<unsigned>(t.count));
            textf(out, sizeof(out), T_ST_TEST_OF, a, b);
            line(v, out);
        }
        break;
    case SelfTestAsk:
        ViewBuilder::addWrapped(&v, text(t.phase == S::AskScreen ? T_ST_BARS_Q : T_ST_BEEPS_Q));
        item(v, text(T_ST_YES), ItemAction);
        item(v, text(T_NO), ItemAction);
        break;
    case SelfTestResults:
        resultsView(t, v);
        break;
    default:
        break;
    }
}

} // namespace gui
} // namespace nbt
