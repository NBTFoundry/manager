#include "nbt/gui/status_page.hpp"

#include "nbt/gui/capture_watch.hpp"
#include "nbt/gui/gamepad_input.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/view_builder.hpp"

#include <string.h>

namespace nbt {
namespace gui {
namespace {

void line(UiView &v, const char *text) {
    if (v.lineCount < kLines) {
        ViewBuilder::formatText(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s", text);
    }
}

int percent(long value, long full) {
    const long p = value * 100 / full;
    return static_cast<int>(p > 100 ? 100 : p < -100 ? -100 : p);
}

void gamepadTest(const StatusFacts &f, UiView &v) {
    const nbt_gamepad_wire_state &s = f.padState;
    ViewBuilder::formatText(v.title, sizeof(v.title), "Gamepad test");
    line(v, f.pad);
    if (!s.connected) {
        if (f.padDetail[0]) {
            line(v, f.padDetail);
        }
        line(v, "Plug a pad into the car's USB port");
    } else {
        char pressed[kTextMax + 1], text[kTextMax + 1];
        buttonNames(f.padFamily, s.buttons, pressed, sizeof(pressed));
        ViewBuilder::formatText(text, sizeof(text), "Pressed: %s", pressed);
        line(v, text);
        ViewBuilder::formatText(text, sizeof(text), "Left stick: x %d, y %d", percent(s.lx, 32767),
                                percent(s.ly, 32767));
        line(v, text);
        ViewBuilder::formatText(text, sizeof(text), "Right stick: x %d, y %d", percent(s.rx, 32767),
                                percent(s.ry, 32767));
        line(v, text);
        ViewBuilder::formatText(text, sizeof(text), "Triggers: left %d, right %d",
                                percent(s.lt, 65535), percent(s.rt, 65535));
        line(v, text);
    }
    line(v, "BACK on the iDrive controller leaves");
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
        ViewBuilder::formatText(out, size, "nothing");
    }
}

void gatherStatus(UiController &ui, const CaptureWatch &capture, const GamepadInput &gamepad,
                  bool controllerInput) {
    StatusFacts &f = ui.facts;
    ViewBuilder::formatText(ui.captureNote, sizeof(ui.captureNote), "%s", capture.note());
    ViewBuilder::formatText(f.keys, sizeof(f.keys), "Keys: %s", capture.describe());
    ViewBuilder::formatText(f.controller, sizeof(f.controller), "iDrive controller: %s",
                            controllerInput ? "read over MOST" : "not read (no MOST input)");
    const gamepad::Service::Report report = gamepad.report();
    GamepadInput::describe(report, f.pad, f.padDetail, sizeof(f.pad));
    f.padFamily = report.family;
    f.padState = gamepad.state();
    if (ui.screen == Error && ui.message[0]) {
        ViewBuilder::formatText(f.lastError, sizeof(f.lastError), "%s", ui.message);
    }
}

bool statusPress(UiController &ui, Key key) {
    const size_t entry = ui.settings.count + 2; // after Pair a phone and Paired phones
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
    ViewBuilder::formatText(v.title, sizeof(v.title), "Status");
    line(v, f.keys);
    line(v, f.controller);
    line(v, f.pad);
    if (f.padDetail[0]) {
        line(v, f.padDetail);
    }
    char text[kTextMax + 1];
    ViewBuilder::formatText(text, sizeof(text), "Last error: %s",
                            f.lastError[0] ? f.lastError : "none since the manager opened");
    line(v, text);
    ViewBuilder::formatText(v.items[v.itemCount++], sizeof(v.items[0]), "Test the gamepad");
}

} // namespace gui
} // namespace nbt
