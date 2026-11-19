#include "nbt/gui/status_page.hpp"

#include "nbt/gui/capture_watch.hpp"
#include "nbt/gui/gamepad_input.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/view_builder.hpp"

#include <string.h>

namespace nbt {
namespace gui {
namespace {

void line(UiView &v, const char *content) {
    if (v.lineCount < kLines) {
        ViewBuilder::formatText(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s", content);
    }
}

/* `id` with two percentages for {1} and {2}. */
void percents(UiView &v, TextId id, long a, long aFull, long b, long bFull) {
    const long pa = a * 100 / aFull, pb = b * 100 / bFull;
    char first[8], second[8];
    ViewBuilder::formatText(first, sizeof(first), "%d",
                            static_cast<int>(pa > 100    ? 100
                                             : pa < -100 ? -100
                                                         : pa));
    ViewBuilder::formatText(second, sizeof(second), "%d",
                            static_cast<int>(pb > 100    ? 100
                                             : pb < -100 ? -100
                                                         : pb));
    if (v.lineCount < kLines) {
        textf(v.lines[v.lineCount++], sizeof(v.lines[0]), id, first, second);
    }
}

void gamepadTest(const StatusFacts &f, UiView &v) {
    const nbt_gamepad_wire_state &s = f.padState;
    textf(v.title, sizeof(v.title), T_GAMEPAD_TEST);
    line(v, f.pad);
    if (!s.connected) {
        if (f.padDetail[0]) {
            line(v, f.padDetail);
        }
        line(v, text(T_PLUG_PAD));
    } else {
        char pressed[kTextMax + 1], shown[kTextMax + 1];
        buttonNames(f.padFamily, s.buttons, pressed, sizeof(pressed));
        textf(shown, sizeof(shown), T_PRESSED, pressed);
        line(v, shown);
        percents(v, T_LEFT_STICK, s.lx, 32767, s.ly, 32767);
        percents(v, T_RIGHT_STICK, s.rx, 32767, s.ry, 32767);
        percents(v, T_TRIGGERS, s.lt, 65535, s.rt, 65535);
    }
    line(v, text(T_BACK_LEAVES));
}

} // namespace

void buttonNames(gamepad::Family family, uint32_t buttons, char *out, size_t size) {
    static const char *const xbox[] = {"A",  "B",  "X",    "Y",  "LB",   "RB",   "View", "Menu",
                                       "LS", "RS", "Xbox", "Up", "Down", "Left", "Right"};
    static const char *const sony[] = {"Cross", "Circle", "Square",  "Triangle", "L1",
                                       "R1",    "Share",  "Options", "L3",       "R3",
                                       "PS",    "Up",     "Down",    "Left",     "Right"};
    const bool isSony = family == gamepad::DualShock4 || family == gamepad::DualSense;
    size_t used = 0;
    if (!size) {
        return;
    }
    out[0] = 0;
    for (unsigned i = 0; i < 15; ++i) {
        if (!(buttons & (1u << i))) {
            continue;
        }
        const char *name =
            isSony ? (i == 6 && family == gamepad::DualSense ? "Create" : sony[i]) : xbox[i];
        const size_t n = strlen(name) + (used ? 1 : 0);
        if (used + n >= size) {
            break; // what fits; the line is short
        }
        ViewBuilder::formatText(out + used, size - used, "%s%s", used ? " " : "", name);
        used += n;
    }
    if (!used) {
        textf(out, size, T_NOTHING);
    }
}

void gatherStatus(UiController &ui, const CaptureWatch &capture, const GamepadInput &gamepad,
                  bool controllerInput) {
    StatusFacts &f = ui.facts;
    ViewBuilder::formatText(ui.captureNote, sizeof(ui.captureNote), "%s", capture.note());
    textf(f.keys, sizeof(f.keys), T_KEYS, capture.describe());
    textf(f.controller, sizeof(f.controller), T_CONTROLLER,
          text(controllerInput ? T_CONTROLLER_MOST : T_CONTROLLER_NONE));
    const gamepad::Service::Report report = gamepad.report();
    GamepadInput::describe(report, f.pad, f.padDetail, sizeof(f.pad));
    f.padFamily = report.family;
    f.padState = gamepad.state();
    if (ui.screen == Error && ui.message[0]) {
        ViewBuilder::formatText(f.lastError, sizeof(f.lastError), "%s", ui.message);
    }
}

bool statusPress(UiController &ui, Key key) {
    const size_t entry = ui.managerEntry(EntryStatus);
    switch (ui.screen) {
    case Settings:
        if (!ui.managerSettings() || key != KeySelect || ui.focus != entry) {
            return false;
        }
        ui.message[0] = 0;
        ui.go(Status);
        return true;
    case Status:
        if (key == KeyBack) {
            ui.go(Settings);
            ui.focus = entry;
        } else if (key == KeySelect) {
            ui.go(GamepadTest);
        }
        return true;
    case GamepadTest:
        if (key == KeyBack) {
            ui.go(Status);
        }
        return true;
    default:
        return false;
    }
}

void statusView(const UiController &ui, UiView &v) {
    const StatusFacts &f = ui.facts;
    if (ui.screen == GamepadTest) {
        gamepadTest(f, v);
        return;
    }
    textf(v.title, sizeof(v.title), T_STATUS);
    line(v, f.keys);
    line(v, f.controller);
    line(v, f.pad);
    if (f.padDetail[0]) {
        line(v, f.padDetail);
    }
    char shown[kTextMax + 1];
    textf(shown, sizeof(shown), T_LAST_ERROR, f.lastError[0] ? f.lastError : text(T_NO_ERROR));
    line(v, shown);
    textf(v.items[v.itemCount++], sizeof(v.items[0]), T_TEST_GAMEPAD);
}

} // namespace gui
} // namespace nbt
