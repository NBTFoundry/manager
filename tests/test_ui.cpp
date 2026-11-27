// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/capture_watch.hpp"
#include "nbt/gui/files_ui.hpp"
#include "nbt/gui/gamepad_input.hpp"
#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/setup_ui.hpp"
#include "nbt/gui/theme.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/ui_renderer.hpp"

using nbt::gui::Frame;
using nbt::gui::Key;
using nbt::gui::Modal;
using nbt::gui::QrCode;
using nbt::gui::Request;
using nbt::gui::Screen;
using nbt::gui::UiController;
using nbt::gui::UiRenderer;
using nbt::gui::UiView;
using nbt::gui::WorkerStatus;

static int initUi(UiController *ui, const char *interfaceName, unsigned port) {
    return ui && ui->init(interfaceName, port).ok() ? 0 : -1;
}
static int nextRequest(UiController *ui, uint64_t now, char *command, size_t size,
                       unsigned *timeout) {
    if (!ui || !command || !timeout || size < 5) {
        return -1;
    }
    Request request;
    int result;
    unsigned background = 0;
    for (;;) {
        result = ui->nextRequest(now, request);
        if (result != 1) {
            break;
        }
        // This legacy UI fixture has no remote phone. Answer background phone
        // queries while individual tests drive their own lifecycle replies.
        if (!strcmp(request.command, "PHONE STATUS")) {
            ui->reply("PHONE\tnone", now);
        } else if (!strcmp(request.command, "SETUP")) {
            ui->reply("SETUP\tcurrent", now);
        } else if (!strcmp(request.command, "STATUS") &&
                   ((ui->screen == nbt::gui::Apps || ui->screen == nbt::gui::Settings ||
                     ui->screen == nbt::gui::Choice) &&
                    !ui->stopping)) {
            ui->reply("STATUS\t0\t-\t-\t-\t0\t-\tIdle", now);
        } else {
            break;
        }
        if (++background == 3) {
            ui->nextPollMs = now + nbt::gui::kPollMs;
            return 0;
        }
    }
    if (result == 1 && strlen(request.command) < size) {
        strcpy(command, request.command);
        *timeout = request.timeoutMs;
        return 1;
    }
    return result == 1 ? -1 : result;
}
static int replyUi(UiController *ui, const char *reply, uint64_t now) {
    if (!ui) {
        return -1;
    }
    const int result = ui->reply(reply, now);
    while (!result && (ui->queued == nbt::gui::R_PHONE_STATUS || ui->queued == nbt::gui::R_SETUP ||
                       (ui->queued == nbt::gui::R_STATUS && ui->screen == nbt::gui::Apps))) {
        Request background;
        if (ui->nextRequest(now, background) != 1) {
            return -1;
        }
        if (ui->reply(!strcmp(background.command, "PHONE STATUS") ? "PHONE\tnone"
                      : !strcmp(background.command, "SETUP")      ? "SETUP\tcurrent"
                                                             : "STATUS\t0\t-\t-\t-\t0\t-\tIdle",
                      now)) {
            return -1;
        }
    }
    return result;
}
static int pressUi(UiController *ui, Key key) {
    return ui ? ui->press(key) : -1;
}
static int releaseUi(UiController *ui, Key key) {
    return ui ? ui->release(key) : 0;
}
static int viewGet(const UiController *ui, UiView *view) {
    return ui && view && ui->view(*view).ok() ? 0 : -1;
}
static int pairingLink(const UiController *ui, char *output, size_t size) {
    return ui && ui->pairingLink(nbt::core::Span<char>(output, size)).ok() ? 0 : -1;
}
static int renderUi(const UiView *view, const QrCode *qr, Frame *frame) {
    UiRenderer renderer;
    return view && frame && renderer.render(*view, qr, *frame).ok() ? 0 : -1;
}
static void setVersion(UiController *ui, const char *version) {
    if (ui) {
        ui->setVersion(version);
    }
}
static void setNotice(UiController *ui, const char *notice) {
    if (ui) {
        ui->setNotice(notice);
    }
}
static bool wantsExit(const UiController *ui) {
    return !ui || ui->wantsExit();
}
static bool installing(const UiController *ui) {
    return ui && ui->installing();
}
static int home(UiController *ui) {
    return ui ? ui->home() : 0;
}
static void requestFailed(UiController *ui) {
    if (ui) {
        ui->requestFailed();
    }
}
static int encodeQr(QrCode &qr, const unsigned char *data, size_t size, int mask) {
    return qr.encode(data, size, mask).ok() ? 0 : -1;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define DIGEST_A "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define DIGEST_B "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"
#define TOKEN "aa11aa11aa11aa11aa11aa11aa11aa11aa11aa11aa11aa11aa11aa11aa11aa11"

static uint64_t now = 1000;
/* What SERVICES answers in the background (exchange). */
static const char *services = "SERVICES\t0";

/* Asserts the next command and answers it. */
static void exchange(UiController *ui, const char *expected, const char *reply) {
    char command[nbt::gui::kCommandMax];
    unsigned timeout = 0;
    const int requestResult = nextRequest(ui, now, command, sizeof(command), &timeout);
    CHECK(requestResult == 1);
    if (strcmp(command, expected)) {
        fprintf(stderr, "got '%s' want '%s'\n", command, expected);
        exit(1);
    }
    CHECK(timeout >= 5000);
    CHECK(replyUi(ui, reply, now) == 0);
    while (ui->queued == nbt::gui::R_PHONE_STATUS || ui->queued == nbt::gui::R_SERVICES ||
           (ui->queued == nbt::gui::R_STATUS && ui->screen == nbt::gui::Apps)) {
        Request background;
        CHECK(ui->nextRequest(now, background) == 1);
        CHECK(ui->reply(!strcmp(background.command, "PHONE STATUS") ? "PHONE\tnone"
                        : !strcmp(background.command, "SERVICES")
                            ? services
                            : "STATUS\t0\t-\t-\t-\t0\t-\tIdle",
                        now) == 0);
    }
}

static void idle(UiController *ui) {
    char command[nbt::gui::kCommandMax];
    unsigned timeout = 0;
    const int requested = nextRequest(ui, now, command, sizeof(command), &timeout);
    if (requested) {
        fprintf(stderr, "idle: got '%s' on screen %d\n", command, static_cast<int>(ui->screen));
    }
    CHECK(requested == 0);
}

static void poll_status(UiController *ui, const char *reply) {
    now += nbt::gui::kPollMs;
    exchange(ui, "STATUS", reply);
}

static UiView view(const UiController *ui) {
    UiView v;
    CHECK(viewGet(ui, &v) == 0);
    return v;
}

static int view_contains(const UiController *ui, const char *needle) {
    UiView v = view(ui);
    size_t i;
    if (strstr(v.title, needle)) {
        return 1;
    }
    for (i = 0; i < v.lineCount; ++i) {
        if (strstr(v.lines[i], needle)) {
            return 1;
        }
    }
    if (strstr(v.code, needle)) {
        return 1;
    }
    for (i = 0; i < v.itemCount; ++i) {
        if (strstr(v.items[i], needle)) {
            return 1;
        }
    }
    return v.modal.shown && (strstr(v.modal.title, needle) || strstr(v.modal.text, needle));
}

/* The view's lines as one text: a long line wraps onto the next. */
static bool lines_contain(const UiController *ui, const char *needle) {
    UiView v = view(ui);
    char all[nbt::gui::kLines * (nbt::gui::kTextMax + 1) + 1] = "";
    for (size_t i = 0; i < v.lineCount; ++i) {
        strcat(all, i ? " " : "");
        strcat(all, v.lines[i]);
    }
    return strstr(all, needle) != NULL;
}

static void press(UiController *ui, Key key, int expected) {
    const int actual = pressUi(ui, key);
    if (actual != expected) {
        fprintf(stderr,
                "press screen=%d key=%d focus=%zu queued=%d outstanding=%d actual=%d expected=%d\n",
                static_cast<int>(ui->screen), static_cast<int>(key), ui->focus, ui->queued,
                ui->outstanding, actual, expected);
    }
    CHECK(actual == expected);
}

#define MANAGER_SETTINGS                                                                           \
    "SETTINGS\t3\trestart\tdebug_log|Debug log|off|off,on\tinvert|Invert knob|off|off,on"          \
    "\trelease|Hand back controller|off|off,on"
#define TEST_SETTINGS "SETTINGS\t1\trestart\tbackground|Background color|blue|blue,red,green,black"

/* OPTION on the focused application: its settings are asked for. The clock
 * moves on as it did for the hold that opened them before (tests that follow
 * count on it). */
static void options(UiController *ui, const char *command, const char *reply) {
    press(ui, nbt::gui::KeyOption, 1);
    now += nbt::gui::kHoldMs;
    exchange(ui, command, reply);
}

/* Starts a controller and loads the main menu. */
static void loaded(UiController *ui, const char *apps, const char *running) {
    CHECK(initUi(ui, "lo", 0) == 0);
    exchange(ui, "LIST", apps);
    CHECK(ui->screen == nbt::gui::Loading);
    exchange(ui, "RUNNING status", running);
    CHECK(ui->screen == nbt::gui::Apps && ui->focus == 0);
}

/* Opens a session as the GUI does for Pair a phone. The main menu has no
 * session entry any more (dev50): a phone's upload opens the worker's session
 * by itself, and the GUI follows it through STATUS; these tests drive the same
 * waiting screen directly. */
static void open_session(UiController *ui, const char *command, const char *reply) {
    ui->queued = nbt::gui::R_SESSION;
    exchange(ui, command, reply);
}

/* The main menu's Settings: the manager's own settings. */
static void manager_settings(UiController *ui) {
    while (ui->focus < ui->appCount) {
        press(ui, nbt::gui::KeyDown, 1);
    }
    press(ui, nbt::gui::KeySelect, 1);
    exchange(ui, "SETTINGS manager live", MANAGER_SETTINGS);
    CHECK(ui->screen == nbt::gui::Settings);
}

/* Settings > Pair a phone: the session gives the Wi-Fi details, then the code. */
static void pair_from_settings(UiController *ui, const char *session_reply) {
    manager_settings(ui);
    while (ui->focus < ui->managerEntry(nbt::gui::EntryPair)) {
        press(ui, nbt::gui::KeyDown, 1);
    }
    CHECK(!strcmp(view(ui).items[ui->focus], "Pair a phone"));
    press(ui, nbt::gui::KeySelect, 1);
    Request req;
    CHECK(ui->nextRequest(now, req) == 1 && !strcmp(req.command, "SESSION lo 0"));
    CHECK(req.timeoutMs >= 15000u);
    CHECK(ui->reply(session_reply, now) == 0);
}

static void verified_review(UiController *ui, const char *digest) {
    char reply[256];
    loaded(ui, "APPS\t2\tdemo\ttest", "RUNNING\t0");
    open_session(ui, "SESSION lo 0", "SESSION\t127.0.0.1:4567");
    CHECK(ui->screen == nbt::gui::Waiting && !ui->app[0]);
    /* Until the package names it, the session has no application. */
    exchange(ui, "STATUS", "STATUS\t1\t-\t-\t-\t0\t-\tReady for upload");
    idle(ui); /* polling waits for the interval */
    snprintf(reply, sizeof(reply),
             "STATUS\t2\ttest\tv1\tv2\t2\t%s\tPackage verified; approve in iDrive", digest);
    poll_status(ui, reply);
    CHECK(ui->screen == nbt::gui::Review && ui->focus == 0 && !strcmp(ui->app, "test"));
    CHECK(!ui->hasWifi());
}

static void test_main_menu_and_banner(void) {
    UiController ui;
    UiView v;
    CHECK(initUi(&ui, "lo", 0) == 0);
    v = view(&ui);
    CHECK(v.busy && !strcmp(v.title, "NBT App Manager dev") && view_contains(&ui, "Loading") &&
          v.progress == -1);
    setVersion(&ui, "v1.2");
    CHECK(!strcmp(view(&ui).title, "NBT App Manager v1.2"));
    setVersion(&ui, "bad version");
    CHECK(!strcmp(ui.version, "dev"));
    setVersion(&ui, "abcdefghijklmnopqrstuvwx");
    CHECK(!strcmp(ui.version, "dev"));
    setVersion(&ui, "dev7");
    exchange(&ui, "LIST", "APPS\t3\tdemo\tnav\ttest");
    exchange(&ui, "RUNNING status", "RUNNING\t1\tnav");
    v = view(&ui);
    CHECK(!strcmp(v.title, "NBT App Manager dev7") && v.itemCount == 4);
    CHECK(!strcmp(v.items[0], "demo") && !strcmp(v.items[1], "nav  (running)") &&
          !strcmp(v.items[2], "test"));
    CHECK(!strcmp(v.items[3], "Settings") && !view_contains(&ui, "Install or update"));
    /* The manager's own actions are drawn apart from the applications. */
    CHECK(v.itemStyle[0] == nbt::gui::ItemEntry && v.itemStyle[2] == nbt::gui::ItemEntry);
    CHECK(v.itemStyle[3] == nbt::gui::ItemAction);
    CHECK(view_contains(&ui, "Push to start, hold to stop; OPTION for settings"));
    /* Settings opens the manager's own settings, under its own banner. */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 0);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::Settings && !strcmp(v.title, "Settings") &&
          !strcmp(v.items[0], "Debug log: off"));
    CHECK(v.itemCount == 9 && !strcmp(v.items[3], "Language: Automatic (English)") &&
          !strcmp(v.items[4], "Pair a phone") && !strcmp(v.items[5], "Paired phones") &&
          !strcmp(v.items[6], "Status") && !strcmp(v.items[7], "Self test") &&
          !strcmp(v.items[8], "Tools") && !view_contains(&ui, "Stop"));
    CHECK(v.itemStyle[3] == nbt::gui::ItemEntry && v.itemStyle[6] == nbt::gui::ItemAction &&
          v.itemStyle[7] == nbt::gui::ItemAction);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 3);
    exchange(&ui, "RUNNING status", "RUNNING\t0"); /* the running state is asked again */
    CHECK(!strcmp(view(&ui).items[1], "nav"));
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(wantsExit(&ui));
}

/* Settings > Status shows what the session gathered; its gamepad test shows
 * the pad's buttons, sticks and triggers and leaves on BACK only. */
static void test_status_page(void) {
    UiController ui;
    loaded(&ui, "APPS\t1\tdemo", "RUNNING\t0");
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    for (unsigned i = 0; i < 6; ++i) {
        press(&ui, nbt::gui::KeyDown, 1);
    }
    /* Status is followed by Self test and Tools (selftest_ui.hpp, tools_ui.hpp). */
    press(&ui, nbt::gui::KeyDown, 1);
    CHECK(ui.focus == ui.managerEntry(nbt::gui::EntrySelfTest));
    press(&ui, nbt::gui::KeyDown, 1);
    CHECK(ui.focus == ui.managerEntry(nbt::gui::EntryTools));
    press(&ui, nbt::gui::KeyUp, 1);
    press(&ui, nbt::gui::KeyUp, 1);
    nbt::gui::StatusFacts &f = ui.facts;
    strcpy(f.keys, "Keys: kept from iDrive");
    strcpy(f.controller, "iDrive controller: read over MOST");
    strcpy(f.pad, "Gamepad: 045e:02d1 could not be opened");
    strcpy(f.padDetail, "  configuration failed; retrying");
    press(&ui, nbt::gui::KeySelect, 1);
    UiView v = view(&ui);
    CHECK(ui.screen == nbt::gui::Status && !strcmp(v.title, "Status") && v.lineCount == 5);
    CHECK(!strcmp(v.lines[0], "Keys: kept from iDrive") &&
          !strcmp(v.lines[3], "  configuration failed; retrying") &&
          !strcmp(v.lines[4], "Last error: none since the manager opened"));
    CHECK(v.itemCount == 1 && !strcmp(v.items[0], "Test the gamepad"));
    CHECK(!strcmp(nbt::gui::screenName(ui.screen), "status"));
    /* No pad: the reason and what to do. */
    press(&ui, nbt::gui::KeySelect, 1);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::GamepadTest && !strcmp(v.title, "Gamepad test") &&
          v.itemCount == 0 && v.lineCount == 4);
    CHECK(!strcmp(v.lines[2], "Plug a pad into the car's USB port") &&
          !strcmp(v.lines[3], "BACK on the iDrive controller leaves"));
    /* A pad: Sony's names for a DualShock 4, sticks and triggers in percent. */
    strcpy(f.pad, "Gamepad: DualShock 4 (054c:09cc)");
    f.padDetail[0] = 0;
    f.padFamily = nbt::gamepad::DualShock4;
    f.padState.connected = 1;
    f.padState.buttons = NBT_GAMEPAD_SOUTH | NBT_GAMEPAD_LB | NBT_GAMEPAD_VIEW | NBT_GAMEPAD_UP;
    f.padState.lx = -32768;
    f.padState.ly = 16384;
    f.padState.rt = 65535;
    v = view(&ui);
    CHECK(v.lineCount == 6 && !strcmp(v.lines[0], "Gamepad: DualShock 4 (054c:09cc)"));
    CHECK(!strcmp(v.lines[1], "Pressed: Cross L1 Share Up"));
    CHECK(!strcmp(v.lines[2], "Left stick: x -100, y 50") &&
          !strcmp(v.lines[3], "Right stick: x 0, y 0") &&
          !strcmp(v.lines[4], "Triggers: left 0, right 100"));
    /* PUSH and the arrows do nothing here; BACK returns to Status, then to
     * Settings on its own entry. */
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyDown, 0);
    CHECK(ui.screen == nbt::gui::GamepadTest);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Status);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == 6);
    /* What the session gathers: here no capture ring, no MOST input and no
     * pad service (host build); the last error screen is kept. */
    nbt::gui::CaptureWatch capture;
    nbt::gui::GamepadInput gamepad;
    ui.fail(0, "Cannot list applications");
    nbt::gui::gatherStatus(ui, capture, gamepad, false);
    CHECK(!strcmp(f.keys, "Keys: unknown") &&
          !strcmp(f.controller, "iDrive controller: not read (no MOST input)"));
    CHECK(!strcmp(f.pad, "Gamepad: not available") && !f.padDetail[0] && !f.padState.connected);
    CHECK(!strcmp(f.lastError, "Cannot list applications") && !ui.captureNote[0]);
    ui.go(nbt::gui::Status);
    nbt::gui::gatherStatus(ui, capture, gamepad, true);
    CHECK(view_contains(&ui, "Last error: Cannot list applications") &&
          view_contains(&ui, "iDrive controller: read over MOST"));
    /* The pad lines from the service's report, most telling cause first. */
    nbt::gamepad::Service::Report r;
    memset(&r, 0, sizeof(r));
    char pad[nbt::gui::kTextMax + 1], detail[nbt::gui::kTextMax + 1];
    const auto described = [&](const char *line, const char *reason) {
        nbt::gui::GamepadInput::describe(r, pad, detail, sizeof(pad));
        return !strcmp(pad, line) && !strcmp(detail, reason);
    };
    CHECK(described("Gamepad: not available", ""));
    r.running = true;
    r.problem.connectError = 19;
    CHECK(described("Gamepad: no USB stack (usbd_connect 19)", ""));
    r.problem.connectError = 0;
    CHECK(described("Gamepad: USB not connected yet", ""));
    r.lockRefused = true;
    CHECK(described("Gamepad: USB held by another process", ""));
    r.lockRefused = false;
    r.owner = true;
    CHECK(described("Gamepad: none found", ""));
    r.problem.vendor = 0x045e;
    r.problem.product = 0x02d1;
    r.problem.step = "configuration failed";
    CHECK(described("Gamepad: 045e:02d1 could not be opened", "  configuration failed; retrying"));
    r.family = nbt::gamepad::Gip; // another pad opened meanwhile
    r.vendor = 0x045e;
    r.product = 0x0b12;
    CHECK(described("Gamepad: Xbox (045e:0b12)", ""));
    /* Button names: what fits, in wire order. */
    char text[nbt::gui::kTextMax + 1];
    nbt::gui::buttonNames(nbt::gamepad::Gip, 0, text, sizeof(text));
    CHECK(!strcmp(text, "nothing"));
    nbt::gui::buttonNames(nbt::gamepad::DualSense, NBT_GAMEPAD_VIEW | NBT_GAMEPAD_GUIDE, text,
                          sizeof(text));
    CHECK(!strcmp(text, "Create PS"));
    nbt::gui::buttonNames(nbt::gamepad::Gip, NBT_GAMEPAD_BUTTONS, text, 12);
    CHECK(!strcmp(text, "A B X Y LB")); // what fits
    nbt::gui::buttonNames(nbt::gamepad::Gip, NBT_GAMEPAD_EAST | NBT_GAMEPAD_RIGHT, text,
                          sizeof(text));
    CHECK(!strcmp(text, "B Right"));
}

/* An application this manager does not run (LIST "<app>|update") is tagged in
 * the menu; starting it says it needs an update, and its page still opens, to
 * uninstall it. */
static void test_application_needing_an_update(void) {
    UiController ui;
    loaded(&ui, "APPS\t2\tdemo|update\ttest", "RUNNING\t0");
    UiView v = view(&ui);
    CHECK(ui.needsUpdate[0] && !ui.needsUpdate[1] &&
          !strcmp(v.items[0], "demo  (needs an update)") && !strcmp(v.items[1], "test"));
    ui.queued = nbt::gui::R_RUNNING; /* running from before the update: its status shows */
    exchange(&ui, "RUNNING status", "RUNNING\t1\tdemo|playing");
    CHECK(!strcmp(view(&ui).items[0], "demo  (playing)"));
    ui.queued = nbt::gui::R_RUNNING;
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 1);
    exchange(&ui, "START demo", "ERR incompatible");
    CHECK(!wantsExit(&ui) && view_contains(&ui, "demo needs an update for this manager"));
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    press(&ui, nbt::gui::KeySelect, 1); /* OK closes the dialog */
    options(&ui, "SETTINGS demo live", "SETTINGS\t0\trestart+update");
    CHECK(ui.screen == nbt::gui::Settings && ui.settingsUpdate);
    CHECK(view_contains(&ui, "demo needs an update for this manager") &&
          view_contains(&ui, "Uninstall demo"));
    /* Any other mark, or an empty one, is a protocol error. */
    const char *bad[] = {"APPS\t1\tdemo|old", "APPS\t1\tdemo|", "APPS\t1\t|update",
                         "APPS\t2\tdemo\tdemo|update"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        UiController other;
        CHECK(initUi(&other, "lo", 0) == 0);
        Request request;
        CHECK(other.nextRequest(now, request) == 1 && !strcmp(request.command, "LIST"));
        CHECK(other.reply(bad[i], now) == -1);
    }
}

static void test_push_starts(void) {
    UiController ui;
    loaded(&ui, "APPS\t2\tdemo\ttest", "RUNNING\t0");
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.holding);
    idle(&ui); /* nothing until the release */
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 1 && !ui.holding);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 0);
    /* Its row says it starts; no line above the list moves it. */
    CHECK(!strcmp(view(&ui).items[1], "test  (starting)") && view(&ui).lineCount == 1);
    exchange(&ui, "START test", "OK");
    /* Started without the screen (in the background): the manager stays on its
     * list, its row showing it running once read, nothing else. */
    CHECK(!wantsExit(&ui) && ui.screen == nbt::gui::Apps && !view(&ui).modal.shown);
    CHECK(!ui.takeHandover());
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    CHECK(!wantsExit(&ui) && !ui.takeAppEnded() && !strcmp(view(&ui).items[1], "test  (running)"));

    loaded(&ui, "APPS\t2\tdemo\ttest", "RUNNING\t0");
    press(&ui, nbt::gui::KeySelect, 1);
    /* Let go just before the hold ends: it starts. */
    now += nbt::gui::kHoldMs - 1;
    idle(&ui);
    CHECK(ui.screen == nbt::gui::Apps && view(&ui).progress == -1);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 1);
    exchange(&ui, "START demo", "ERR start");
    /* A failure is a dialog over the list: OK (the push or BACK) closes it and
     * starts nothing; the other keys wait for it. */
    UiView v = view(&ui);
    CHECK(!wantsExit(&ui) && v.modal.shown && !strcmp(v.modal.text, "Cannot start demo") &&
          v.modal.itemCount == 1 && !strcmp(v.modal.items[0], "OK") && v.lineCount == 1);
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    press(&ui, nbt::gui::KeyDown, 0);
    press(&ui, nbt::gui::KeyOption, 0);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(!view(&ui).modal.shown && releaseUi(&ui, nbt::gui::KeySelect) == 0 && !ui.queued);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 1);
    exchange(&ui, "START demo", "ERR running");
    CHECK(!wantsExit(&ui) && view_contains(&ui, "demo is already running"));
    exchange(&ui, "RUNNING status", "RUNNING\t1\tdemo");
    CHECK(!wantsExit(&ui) && view_contains(&ui, "demo is already running"));
    press(&ui, nbt::gui::KeyBack, 1); /* BACK closes the dialog, not the manager */
    CHECK(!wantsExit(&ui) && !view(&ui).modal.shown);
    /* Moving away cancels a hold; other keys and releases do nothing. */
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    CHECK(!ui.holding && releaseUi(&ui, nbt::gui::KeySelect) == 0);
    CHECK(releaseUi(&ui, nbt::gui::KeyBack) == 0 && releaseUi(NULL, nbt::gui::KeySelect) == 0);
    idle(&ui);
}

/* Background services (docs/service-bridge.md): an application whose service
 * runs alone says so on its row; holding the push stops it. */
static void test_service_rows(void) {
    UiController ui;
    services = "SERVICES\t1\ttest";
    loaded(&ui, "APPS\t2\tdemo\ttest", "RUNNING\t0");
    now += nbt::gui::kPollMs; /* the next poll asks which run only their service */
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(!strcmp(view(&ui).items[0], "demo") &&
          !strcmp(view(&ui).items[1], "test  (service running)"));
    CHECK(nbt::gui::ReplyParser::parseServices(ui, "SERVICES\t1\tother") == -1);
    CHECK(nbt::gui::ReplyParser::parseServices(ui, "SERVICES\t1\ttest|x") == -1);
    CHECK(nbt::gui::ReplyParser::parseServices(ui, "SERVICES\t2\ttest\ttest") == -1);
    CHECK(ui.service[1] && !ui.service[0]);
    press(&ui, nbt::gui::KeyDown, 1);
    idle(&ui);
    const uint64_t pressed = now;
    press(&ui, nbt::gui::KeySelect, 1);
    now = pressed + nbt::gui::kHoldMs;
    exchange(&ui, "STOP test", "OK");
    CHECK(!strcmp(view(&ui).items[1], "test  (stopping)"));
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 0 && !ui.queued);
    services = "SERVICES\t0";
}

static void test_hold_stops_and_option_opens_settings(void) {
    UiController ui;
    UiView v;
    uint64_t pressed;
    loaded(&ui, "APPS\t2\tdemo\ttest", "RUNNING\t1\ttest");
    /* Holding the push on an application that does not run shows nothing, and
     * letting go then starts nothing. */
    idle(&ui);
    pressed = now;
    press(&ui, nbt::gui::KeySelect, 1);
    now = pressed + nbt::gui::kHoldMs / 2;
    idle(&ui);
    CHECK(!view(&ui).modal.shown);
    now = pressed + nbt::gui::kHoldMs;
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest"); /* the usual poll, nothing else */
    CHECK(!ui.holding && !ui.queued && ui.screen == nbt::gui::Apps);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 0 && !ui.queued);
    /* On a running one, after half a second a dialog over the menu fills a bar
     * until it is stopped. */
    press(&ui, nbt::gui::KeyDown, 1);
    idle(&ui);
    pressed = now;
    press(&ui, nbt::gui::KeySelect, 1);
    now = pressed + nbt::gui::kHoldShowMs - 1;
    idle(&ui);
    CHECK(!view(&ui).modal.shown && view(&ui).progress == -1);
    now = pressed + nbt::gui::kHoldMs / 2;
    idle(&ui);
    v = view(&ui);
    CHECK(v.modal.shown && v.modal.progress == 50 && !strcmp(v.modal.title, "Stop test"));
    CHECK(!strcmp(v.modal.text, "Keep holding to stop it") && !v.modal.itemCount &&
          v.progress == -1);
    CHECK(!strcmp(v.title, "NBT App Manager dev") && v.focus == 1 && v.itemCount == 3);
    now = pressed + nbt::gui::kHoldMs;
    exchange(&ui, "STOP test", "OK");
    /* Its row says it stops; no line moves the list, no dialog. */
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::Apps && !strcmp(v.items[1], "test  (stopping)") &&
          v.lineCount == 1 && !v.modal.shown);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 0 && !ui.queued); /* starts nothing */
    poll_status(&ui, "STATUS\t0\ttest\tv1\t-\t0\t-\tApplication stopped");
    CHECK(ui.screen == nbt::gui::Apps && !view_contains(&ui, "test stopped") &&
          !view(&ui).modal.shown);
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest"); /* the fixture runs it again */
    CHECK(!strcmp(view(&ui).items[1], "test  (running)"));
    /* OPTION opens the focused application's settings. */
    press(&ui, nbt::gui::KeyOption, 1);
    exchange(&ui, "SETTINGS test live", TEST_SETTINGS);
    v = view(&ui);
    /* A running application's settings: its own, Start with the car, App shortcut, then
     * stopping and uninstalling it. */
    CHECK(ui.screen == nbt::gui::Settings && !v.modal.shown && !strcmp(v.title, "test settings") &&
          v.itemCount == 5);
    CHECK(!strcmp(v.items[0], "Background color: blue") &&
          !strcmp(v.items[1], "Start with the car: off") &&
          !strcmp(v.items[2], "App shortcut: off") && !strcmp(v.items[3], "Stop test") &&
          !strcmp(v.items[4], "Uninstall test"));
    CHECK(v.itemStyle[0] == nbt::gui::ItemEntry && v.itemStyle[1] == nbt::gui::ItemEntry &&
          v.itemStyle[2] == nbt::gui::ItemEntry && v.itemStyle[3] == nbt::gui::ItemAction &&
          v.itemStyle[4] == nbt::gui::ItemAction && v.focus == 0);
    CHECK(view_contains(&ui, "Changes apply when test next starts"));
    /* OPTION does nothing on the settings page. */
    press(&ui, nbt::gui::KeyOption, 0);
    /* BACK returns to the main menu, on the application. */
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 1);
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    /* Stopping is an entry like the settings. */
    options(&ui, "SETTINGS test live", TEST_SETTINGS);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 0);
    press(&ui, nbt::gui::KeyUp, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Settings && view_contains(&ui, "Stopping test"));
    exchange(&ui, "STOP test", "OK");
    CHECK(view(&ui).busy);
    press(&ui, nbt::gui::KeySelect, 0); /* busy until the stop has finished */
    press(&ui, nbt::gui::KeyBack, 0);
    exchange(&ui, "STATUS", "STATUS\t0\ttest\tv1\t-\t0\t-\tStopping application");
    poll_status(&ui, "STATUS\t0\ttest\tv1\t-\t0\t-\tApplication stopped");
    CHECK(ui.screen == nbt::gui::Settings && view_contains(&ui, "test stopped"));
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    v = view(&ui);
    /* Focus stays on the fourth item, now Uninstall (which asks first). */
    CHECK(v.itemCount == 4 && !strcmp(v.items[0], "Background color: blue") && v.focus == 3 &&
          !strcmp(v.items[3], "Uninstall test") && !v.busy);
    idle(&ui);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 1 && !view_contains(&ui, "test stopped"));
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(!strcmp(view(&ui).items[1], "test"));
    /* Nothing to set and not running: Start with the car and Uninstall; BACK leaves. */
    press(&ui, nbt::gui::KeyUp, 1);
    options(&ui, "SETTINGS demo live", "SETTINGS\t0\trestart");
    v = view(&ui);
    CHECK(!strcmp(v.title, "demo settings") && view_contains(&ui, "demo has no settings"));
    CHECK(v.itemCount == 3 && !strcmp(v.items[0], "Start with the car: off") &&
          !strcmp(v.items[1], "App shortcut: off") && !strcmp(v.items[2], "Uninstall demo") &&
          v.itemStyle[2] == nbt::gui::ItemAction);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 0);
}

static void test_application_on_the_panel_ends(void) {
    UiController ui;
    loaded(&ui, "APPS\t1\ttest", "RUNNING\t0");
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 1);
    exchange(&ui, "START test", "OK screen");
    CHECK(ui.takeHandover() && !ui.takeHandover() && !ui.takeAppEnded());
    /* Not running yet is not an end; running, then gone, is (BACK in it). */
    now += nbt::gui::kPollMs;
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(!ui.takeAppEnded());
    now += nbt::gui::kPollMs;
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    CHECK(!ui.takeAppEnded());
    now += nbt::gui::kPollMs;
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(ui.takeAppEnded() && !ui.takeAppEnded());
    /* Without a hand-over, an application ending means nothing here. */
    now += nbt::gui::kPollMs;
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    now += nbt::gui::kPollMs;
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(!ui.takeAppEnded());
}

static void test_new_application(void) {
    UiController ui;
    char approve[160];
    loaded(&ui, "APPS\t0", "RUNNING\t0");
    open_session(&ui, "SESSION lo 0", "SESSION\t127.0.0.1:4567");
    /* Nothing of it installed: a new application, signed with the apps key. */
    poll_status(&ui,
                "STATUS\t2\ttest\t-\tv2\t2\t" DIGEST_A "\tPackage verified; approve in iDrive");
    CHECK(!strcmp(view(&ui).title, "Review new application") && view_contains(&ui, "Version v2") &&
          view(&ui).itemCount == 2); /* no "even if older" for a new application */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1); /* Install: the one confirmation */
    snprintf(approve, sizeof(approve), "APPROVE %s normal", DIGEST_A);
    exchange(&ui, approve, "OK installing");
    poll_status(&ui, "STATUS\t4\ttest\tv2\t-\t0\t-\tApplication installed");
    CHECK(ui.screen == nbt::gui::Result && view_contains(&ui, "v2"));
    /* Back on the main menu, the list is read again and has it. */
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "LIST", "APPS\t1\ttest");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(!strcmp(view(&ui).items[0], "test"));
    /* A reason too long for one line is wrapped at a space, not cut. */
    open_session(&ui, "SESSION lo 0", "SESSION\t127.0.0.1:4567");
    poll_status(&ui, "STATUS\t5\t-\t-\t-\t0\t-\tPackage rejected: package not signed by an "
                     "authorized key for this application");
    UiView v = view(&ui);
    CHECK(ui.screen == nbt::gui::Result && v.lineCount == 3);
    CHECK(!strcmp(v.lines[1], "Package rejected: package not signed by an") &&
          !strcmp(v.lines[2], "authorized key for this application"));
}

static void test_uninstall(void) {
    UiController ui;
    UiView v;
    loaded(&ui, "APPS\t2\tdemo\ttest", "RUNNING\t0");
    options(&ui, "SETTINGS demo live", "SETTINGS\t0\trestart");
    /* Uninstall (after Start with the car and App shortcut) asks first, with No focused. */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::Uninstall && !strcmp(v.title, "Uninstall demo?") && v.focus == 0);
    CHECK(v.itemCount == 2 && !strcmp(v.items[0], "No") && !strcmp(v.items[1], "Yes, uninstall"));
    CHECK(view_contains(&ui, "demo and all its data are deleted") && !ui.queued);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == ui.uninstallItem() && !ui.queued);
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Settings && !ui.queued);
    /* The worker's refusals come back to the settings with the reason. */
    const char *const refusals[][2] = {{"ERR running", "Stop demo first"},
                                       {"ERR busy", "Busy; uninstall demo later"},
                                       {"ERR remove", "Cannot uninstall demo"}};
    for (size_t i = 0; i < sizeof(refusals) / sizeof(refusals[0]); ++i) {
        press(&ui, nbt::gui::KeySelect, 1);
        press(&ui, nbt::gui::KeyDown, 1);
        press(&ui, nbt::gui::KeySelect, 1);
        CHECK(view_contains(&ui, "Uninstalling demo") && view(&ui).busy);
        press(&ui, nbt::gui::KeySelect, 0);
        exchange(&ui, "REMOVE demo", refusals[i][0]);
        CHECK(ui.screen == nbt::gui::Settings && view_contains(&ui, refusals[i][1]));
        exchange(&ui, "RUNNING status", "RUNNING\t0");
    }
    /* Done: the main menu without it. */
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "REMOVE demo", "OK");
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 0 && view_contains(&ui, "demo uninstalled"));
    exchange(&ui, "LIST", "APPS\t1\ttest");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    v = view(&ui);
    CHECK(ui.appCount == 1 && !strcmp(v.items[0], "test") &&
          view_contains(&ui, "demo uninstalled") && v.modal.shown);
    press(&ui, nbt::gui::KeySelect, 1); /* OK */
    /* The manager's own settings offer no Uninstall. */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    CHECK(view(&ui).itemCount == 9 && !view_contains(&ui, "Uninstall"));
}

static void test_stop_failures(void) {
    UiController ui;
    loaded(&ui, "APPS\t1\ttest", "RUNNING\t1\ttest");
    options(&ui, "SETTINGS test live", "SETTINGS\t0\trestart");
    CHECK(view(&ui).itemCount == 4 && !strcmp(view(&ui).items[2], "Stop test"));
    CHECK(view_contains(&ui, "test has no settings"));
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "STOP test", "ERR stop");
    CHECK(ui.screen == nbt::gui::Settings && view_contains(&ui, "Cannot stop test"));
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "STOP test", "OK");
    exchange(&ui, "STATUS", "STATUS\t5\ttest\tv1\t-\t0\t-\tcannot stop application");
    CHECK(view_contains(&ui, "Cannot stop test"));
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    /* Unreadable settings leave the main menu with a message. */
    press(&ui, nbt::gui::KeyBack, 1);
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    options(&ui, "SETTINGS test live", "ERR settings");
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 0 &&
          view_contains(&ui, "Cannot read test settings") && view(&ui).modal.shown);
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    idle(&ui);
    press(&ui, nbt::gui::KeyDown, 0); /* the dialog first */
    press(&ui, nbt::gui::KeyBack, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SETTINGS manager live", "ERR settings");
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 1 &&
          view_contains(&ui, "Cannot read settings"));
}

static void test_install_flow(void) {
    UiController ui;
    char link[160], approve[160];
    loaded(&ui, "APPS\t1\ttest", "RUNNING\t0");
    open_session(&ui, "SESSION lo 0", "SESSION\t127.0.0.1:4567");
    CHECK(!strcmp(view(&ui).title, "Install or update"));
    CHECK(ui.updating()); /* from the session on, the GUI does not close for idling */
    CHECK(view_contains(&ui, "Waiting for a paired phone"));
    CHECK(pairingLink(&ui, link, sizeof(link)) == -1);
    poll_status(&ui,
                "STATUS\t2\ttest\tv1\tv2\t2\t" DIGEST_A "\tPackage verified; approve in iDrive");
    CHECK(pairingLink(&ui, link, sizeof(link)) == -1);
    CHECK(!strcmp(view(&ui).title, "Review update"));
    /* What the driver can act on, and nothing internal (no hash, no sequence). */
    CHECK(view_contains(&ui, "Installed: v1, new: v2") && !view_contains(&ui, "01234567") &&
          !view_contains(&ui, "sequence") && view(&ui).itemCount == 3);
    /* An update lists only the permissions it adds; driving says how far control goes. */
    CHECK(!view_contains(&ui, "Uses:") && !view_contains(&ui, "can:"));
    poll_status(&ui, "STATUS\t2\ttest\tv1\tv2\t2\t" DIGEST_A
                     "\tPackage verified; approve in iDrive\t-\tperm:e0e:a02");
    CHECK(lines_contain(&ui, "Now also uses: car data, phone screen") &&
          lines_contain(&ui, "Now also can: control the car (even while driving)") &&
          !lines_contain(&ui, "engine data"));
    /* Install is the one confirmation: no second screen. */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Progress && view_contains(&ui, "Installing test v2") &&
          ui.updating());
    CHECK(installing(&ui) && home(&ui) == 0 && !wantsExit(&ui));
    snprintf(approve, sizeof(approve), "APPROVE %s normal", DIGEST_A);
    exchange(&ui, approve, "OK installing");
    exchange(&ui, "STATUS", "STATUS\t3\ttest\tv1\tv2\t2\t" DIGEST_A "\tStopping application");
    CHECK(view_contains(&ui, "Stopping application") && view(&ui).progress == -1);
    now += nbt::gui::kPollMs;
    exchange(&ui, "STATUS",
             "STATUS\t3\ttest\tv1\tv2\t2\t" DIGEST_A "\tChecking application health");
    /* The 30 s health check fills a bar, which stops short of full. */
    CHECK(view(&ui).progress == 0);
    now += nbt::gui::kAppHealthMs / 2 - nbt::gui::kPollMs;
    poll_status(&ui, "STATUS\t3\ttest\tv1\tv2\t2\t" DIGEST_A "\tChecking application health");
    CHECK(view(&ui).progress == 50);
    now += nbt::gui::kAppHealthMs;
    poll_status(&ui, "STATUS\t3\ttest\tv1\tv2\t2\t" DIGEST_A "\tStopping application");
    CHECK(view(&ui).progress == 99);
    press(&ui, nbt::gui::KeyBack, 0);
    poll_status(&ui, "STATUS\t4\ttest\tv2\t-\t0\t-\tUpdate installed");
    CHECK(ui.screen == nbt::gui::Result && view_contains(&ui, "Update installed") &&
          view_contains(&ui, "v2"));
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 1 && !installing(&ui) && !ui.app[0] &&
          !ui.updating());
    exchange(&ui, "LIST", "APPS\t1\ttest"); /* reloaded: an install may add one */
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest");
    idle(&ui);
    CHECK(home(&ui) == 1 && wantsExit(&ui) && home(NULL) == 0);
}

static void test_recovery_and_reject(void) {
    UiController ui;
    verified_review(&ui, DIGEST_A);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.recovery && view_contains(&ui, "Confirm older version") &&
          view_contains(&ui, "older version"));
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "APPROVE " DIGEST_A " recovery", "ERR install"); /* rejected by the worker */
    CHECK(ui.screen == nbt::gui::Result && view_contains(&ui, "Installation failed"));
    exchange(&ui, "STATUS", "STATUS\t5\ttest\tv1\t-\t0\t-\tCandidate failed; restored");
    CHECK(view_contains(&ui, "restored"));

    verified_review(&ui, DIGEST_A);
    press(&ui, nbt::gui::KeySelect, 1); /* Reject has default focus */
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 2 && view_contains(&ui, "rejected"));
    exchange(&ui, "CANCEL", "OK");
    idle(&ui);

    verified_review(&ui, DIGEST_A);
    press(&ui, nbt::gui::KeyBack, 1);
    exchange(&ui, "CANCEL", "OK");
}

static void test_package_change_requires_new_review(void) {
    UiController ui;
    char command[nbt::gui::kCommandMax];
    unsigned timeout;
    verified_review(&ui, DIGEST_A);
    press(&ui, nbt::gui::KeyDown, 1); /* on Install */
    poll_status(&ui,
                "STATUS\t2\ttest\tv1\tv3\t3\t" DIGEST_B "\tPackage verified; approve in iDrive");
    CHECK(ui.screen == nbt::gui::Review && ui.focus == 0 && !strcmp(ui.review.digest, DIGEST_B));
    CHECK(view_contains(&ui, "new: v3")); /* the review shows the new package */
    /* Another application's package ends the review. */
    poll_status(&ui,
                "STATUS\t2\tdemo\tv1\tv3\t3\t" DIGEST_B "\tPackage verified; approve in iDrive");
    CHECK(ui.screen == nbt::gui::Result);
    exchange(&ui, "CANCEL", "OK");

    /* A reply that arrives while the user confirms must cancel the approval. */
    verified_review(&ui, DIGEST_A);
    press(&ui, nbt::gui::KeyDown, 1);
    now += nbt::gui::kPollMs;
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 1 &&
          !strcmp(command, "STATUS"));
    press(&ui, nbt::gui::KeySelect, 0); /* busy while the poll is outstanding */
    CHECK(replyUi(&ui, "STATUS\t0\t-\t-\t-\t0\t-\tSession closed", now) == 0);
    CHECK(ui.screen == nbt::gui::Result);
    exchange(&ui, "CANCEL", "OK");
    idle(&ui);

    /* Confirmation re-checks the last reported package. */
    verified_review(&ui, DIGEST_A);
    press(&ui, nbt::gui::KeyDown, 1);
    strcpy(ui.status.digest, DIGEST_B);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Review && !strcmp(ui.review.digest, DIGEST_B));
    idle(&ui);
    press(&ui, nbt::gui::KeyDown, 1);
    ui.status.state = 0;
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Apps && view_contains(&ui, "Session ended"));
    exchange(&ui, "CANCEL", "OK");
}

static void test_pairing_exits(void) {
    UiController ui;
    CHECK(initUi(&ui, "uap0", 8080) == 0);
    exchange(&ui, "LIST", "APPS\t1\ttest");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    open_session(&ui, "SESSION uap0 8080", "ERR network-unavailable");
    CHECK(ui.screen == nbt::gui::Apps && view_contains(&ui, "Wi-Fi"));
    open_session(&ui, "SESSION uap0 8080", "SESSION\t10.0.0.1:8080");
    exchange(&ui, "STATUS",
             "STATUS\t5\t-\t-\t-\t0\t-\tPackage rejected: not an NBT Manager package");
    CHECK(ui.screen == nbt::gui::Result && view_contains(&ui, "Upload rejected") &&
          view_contains(&ui, "not an NBT"));
    CHECK(!ui.hasWifi() && !ui.host[0]);
    exchange(&ui, "CANCEL", "OK");
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "LIST", "APPS\t1\ttest");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    open_session(&ui, "SESSION uap0 8080", "SESSION\t10.0.0.1:8080");
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 1 && !ui.hasWifi());
    exchange(&ui, "CANCEL", "OK");
    /* What ended it is a dialog: BACK closes it, then the manager. */
    CHECK(view(&ui).modal.shown);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(!wantsExit(&ui) && !view(&ui).modal.shown);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(wantsExit(&ui));
}

/* The car's Wi-Fi Direct group comes first: its Wi-Fi code, then the link. */
static void test_pairing_wifi(void) {
    UiController ui;
    char text[256];
    loaded(&ui, "APPS\t1\ttest", "RUNNING\t0");
    pair_from_settings(&ui, "SESSION\t172.16.222.1:8080\tDIRECT-03-BMW34567\tk3;Z:9,x\\Q");
    CHECK(ui.hasWifi());
    exchange(&ui, "PHONE OPEN", "OK"); /* straight to the code, no waiting screen */
    CHECK(ui.screen == nbt::gui::Pairing && view_contains(&ui, "Scan this Wi-Fi code"));
    CHECK(pairingLink(&ui, text, sizeof(text)) == 0 &&
          !strcmp(text, "WIFI:T:WPA;S:DIRECT-03-BMW34567;P:k3\\;Z\\:9\\,x\\\\Q;;"));
    CHECK(!view_contains(&ui, "k3;Z") && !view_contains(&ui, "install link"));
    ui.queued = nbt::gui::R_PHONE_STATUS;
    Request request;
    CHECK(ui.nextRequest(now, request) == 1);
    CHECK(!strcmp(request.command, "PHONE STATUS"));
    CHECK(ui.reply("PHONE\tpair\t0123456789abcdef0123456789abcdef\tPixel\t123456", now) == 0);
    CHECK(ui.screen == nbt::gui::PhoneConfirm && ui.focus == 0 && view_contains(&ui, "123 456"));
    CHECK(!strcmp(view(&ui).code, "123 456")); /* drawn large, as on the phone */
    CHECK(!ui.hasWifi() && pairingLink(&ui, text, sizeof(text)) == -1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "PHONE NO 0123456789abcdef0123456789abcdef", "OK cancelled");
    CHECK(ui.screen == nbt::gui::Result && view_contains(&ui, "Request rejected"));
    exchange(&ui, "CANCEL", "OK");
}

/* SETUP once the list shows; a replaced plugin asks to restart iDrive, Later focused. */
static void test_setup_and_idrive_restart(void) {
    UiController ui;
    Request request;
    CHECK(initUi(&ui, "lo", 0) == 0);
    exchange(&ui, "LIST", "APPS\t1\ttest");
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "RUNNING status"));
    CHECK(ui.reply("RUNNING\t0", now) == 0 && ui.screen == nbt::gui::Apps);
    /* SETUP at once, before any other request: the menu is busy until it answers. */
    CHECK(ui.queued == nbt::gui::R_SETUP && view(&ui).busy);
    press(&ui, nbt::gui::KeySelect, 0);
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "SETUP"));
    CHECK(request.timeoutMs >= 60000u && view(&ui).busy);
    CHECK(ui.reply("SETUP\ttrial", now) == 0 && ui.screen == nbt::gui::Apps);
    /* On trial: the list shows the check in a modal, its progress over the health check,
     * and holds the menu: no key does anything. */
    UiView checking = view(&ui);
    CHECK(checking.modal.shown && !strcmp(checking.modal.title, "Updating NBT Manager") &&
          strstr(checking.modal.text, "Checking version") && checking.modal.progress == 0);
    press(&ui, nbt::gui::KeyDown, 0);
    press(&ui, nbt::gui::KeySelect, 0);
    press(&ui, nbt::gui::KeyOption, 0);
    press(&ui, nbt::gui::KeyBack, 0);
    CHECK(!wantsExit(&ui) && !ui.queued && ui.focus == 0);
    now += nbt::gui::kPollMs;
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "RUNNING status"));
    CHECK(ui.reply("RUNNING\t0", now) == 0);
    while (ui.nextRequest(now, request) == 1) { // the background phone and status polls
        CHECK(ui.reply(!strcmp(request.command, "PHONE STATUS") ? "PHONE\tnone"
                       : !strcmp(request.command, "SERVICES")   ? "SERVICES\t0"
                                                                : "STATUS\t0\t-\t-\t-\t0\t-\tIdle",
                       now) == 0);
    }
    now += nbt::gui::kTrialPollMs;
    ui.clockMs = now;
    CHECK(view(&ui).modal.shown && view(&ui).modal.progress > 0 && view(&ui).modal.progress < 100);
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "SETUP"));
    CHECK(ui.reply("SETUP\ttrial", now) == 0 && ui.setupDue);
    now += nbt::gui::kTrialPollMs;
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "SETUP"));
    CHECK(ui.reply("SETUP\tboth", now) == 0 && ui.screen == nbt::gui::RestartIdrive);
    CHECK(!ui.trialSinceMs && !view(&ui).modal.shown);
    UiView v = view(&ui);
    CHECK(!strcmp(v.title, "Restart head unit?") && v.focus == 0 && v.itemCount == 2 &&
          !strcmp(v.items[0], "Later") && !strcmp(v.items[1], "Restart head unit"));
    press(&ui, nbt::gui::KeySelect, 1); /* Later */
    CHECK(ui.screen == nbt::gui::Apps && !ui.queued);
    ui.go(nbt::gui::RestartIdrive);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Apps && !ui.queued);
    ui.go(nbt::gui::RestartIdrive);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "IDRIVE RESTART"));
    CHECK(ui.reply("OK", now) == 0 && ui.screen == nbt::gui::Result &&
          view_contains(&ui, "Restarting the head unit"));
    CHECK(!strcmp(view(&ui).title, "Install or update")); /* not Tools (tools_ui.hpp) */
    /* Checked once per opening, apart from the retries during a trial. */
    CHECK(!ui.setupDue);
}

/* One bundled application through a self test run: started as START does,
 * followed in RUNNING (it is never in LIST), until it ends. */
static void self_test_app(UiController *ui, const char *app) {
    char start[64];
    snprintf(start, sizeof(start), "START %s", app);
    now += nbt::gui::kPollMs;
    exchange(ui, start, "OK screen");
    CHECK(ui->takeHandover() && ui->appRun == 1);
    now += nbt::gui::kPollMs;
    char running[80];
    snprintf(running, sizeof(running), "RUNNING\t1\t%s|Self test", app);
    exchange(ui, "RUNNING status", running);
    CHECK(ui->appRun == 2 && ui->targetRunning);
    now += nbt::gui::kPollMs;
    exchange(ui, "RUNNING status", "RUNNING\t0");
    CHECK(ui->appRun == 3 && ui->takeAppEnded() && !ui->appRun);
}

/* Settings > Self test (selftest_ui.hpp, docs/self-test.md): the worker
 * prepares the bundled applications, each runs on the panel in turn, the
 * module after its instruction, two questions, then what did not pass. */
static void test_self_test(void) {
    UiController ui;
    Request request;
    loaded(&ui, "APPS\t1\tdemo", "RUNNING\t0");
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    CHECK(ui.managerEntry(nbt::gui::EntrySelfTest) == 7 && view_contains(&ui, "Self test"));
    ui.focus = ui.managerEntry(nbt::gui::EntrySelfTest);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SELFTEST LAST", "SELFTEST\tnone");
    UiView v = view(&ui);
    CHECK(ui.screen == nbt::gui::SelfTest && !strcmp(v.title, "Self test") && v.itemCount == 1 &&
          !strcmp(v.items[0], "Start the self test") && !view_contains(&ui, "Last test"));
    /* A malformed PREPARE reply is a protocol error, nothing runs. */
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "SELFTEST PREPARE") &&
          request.timeoutMs == 60000);
    CHECK(ui.reply("SELFTEST\tready\tdemo", now) == -1);
    /* Busy: back on its menu, saying so. */
    UiController busy;
    loaded(&busy, "APPS\t1\tdemo", "RUNNING\t0");
    press(&busy, nbt::gui::KeyDown, 1);
    press(&busy, nbt::gui::KeySelect, 1);
    exchange(&busy, "SETTINGS manager live", MANAGER_SETTINGS);
    busy.focus = busy.managerEntry(nbt::gui::EntrySelfTest);
    press(&busy, nbt::gui::KeySelect, 1);
    exchange(&busy, "SELFTEST LAST", "SELFTEST\t20\t1\t0\tmodule.controller|f|no turn in 15 s");
    CHECK(view(&busy).itemCount == 2 && view_contains(&busy, "Last test: 20 passed, 1 failed"));
    press(&busy, nbt::gui::KeySelect, 1);
    exchange(&busy, "SELFTEST PREPARE", "ERR busy");
    CHECK(busy.screen == nbt::gui::SelfTest &&
          view_contains(&busy, "NBT Manager is busy; try again in a moment"));
    /* An application running would hold the screen: stopped first (no
     * fault), then the preparation. */
    UiController held;
    loaded(&held, "APPS\t1\tdemo", "RUNNING\t1\tdemo");
    press(&held, nbt::gui::KeyDown, 1);
    press(&held, nbt::gui::KeySelect, 1);
    exchange(&held, "SETTINGS manager live", MANAGER_SETTINGS);
    held.focus = held.managerEntry(nbt::gui::EntrySelfTest);
    press(&held, nbt::gui::KeySelect, 1);
    exchange(&held, "SELFTEST LAST", "SELFTEST\tnone");
    CHECK(view_contains(&held, "Running applications are stopped first"));
    press(&held, nbt::gui::KeySelect, 1);
    CHECK(held.screen == nbt::gui::SelfTestRun && view_contains(&held, "Stopping demo"));
    exchange(&held, "STOP demo", "OK");
    now += nbt::gui::kPollMs;
    exchange(&held, "RUNNING status", "RUNNING\t1\tdemo");
    now += nbt::gui::kPollMs;
    exchange(&held, "RUNNING status", "RUNNING\t0");
    CHECK(!held.stopping);
    exchange(&held, "SELFTEST PREPARE", "SELFTEST\tready\t");
    /* The whole run. */
    UiController run;
    loaded(&run, "APPS\t1\tdemo", "RUNNING\t0");
    press(&run, nbt::gui::KeyDown, 1);
    press(&run, nbt::gui::KeySelect, 1);
    exchange(&run, "SETTINGS manager live", MANAGER_SETTINGS);
    run.focus = run.managerEntry(nbt::gui::EntrySelfTest);
    press(&run, nbt::gui::KeySelect, 1);
    exchange(&run, "SELFTEST LAST", "SELFTEST\tnone");
    press(&run, nbt::gui::KeySelect, 1);
    CHECK(run.screen == nbt::gui::SelfTestRun && view(&run).busy &&
          view_contains(&run, "Preparing the test applications..."));
    exchange(&run, "SELFTEST PREPARE",
             "SELFTEST\tready\tmanager-selftest-native,manager-selftest-guest,manager-selftest");
    self_test_app(&run, "manager-selftest-native");
    CHECK(view_contains(&run, "Test 1 of 3"));
    /* One that never shows: stopped without a fault, then the next. */
    now += nbt::gui::kPollMs;
    exchange(&run, "START manager-selftest-guest", "OK screen");
    CHECK(run.takeHandover());
    run.abortApp(nbt::gui::T_APP_NOT_SHOWN);
    CHECK(run.screen == nbt::gui::SelfTestRun && run.takeAppEnded() && !run.appRun);
    exchange(&run, "STOP manager-selftest-guest", "OK");
    now += nbt::gui::kPollMs;
    exchange(&run, "RUNNING status", "RUNNING\t1\tmanager-selftest-guest");
    now += nbt::gui::kPollMs;
    exchange(&run, "RUNNING status", "RUNNING\t0");
    CHECK(!run.stopping);
    /* The module: its instruction first. */
    now += nbt::gui::kPollMs;
    CHECK(run.nextRequest(now, request) == 0);
    v = view(&run);
    CHECK(!v.busy && v.itemCount == 1 && !strcmp(v.items[0], "Continue") &&
          view_contains(&run, "Next: colour bars and three beeps"));
    press(&run, nbt::gui::KeySelect, 1);
    self_test_app(&run, "manager-selftest");
    /* The two questions: Yes to the bars, No to the beeps. */
    now += nbt::gui::kPollMs;
    CHECK(run.nextRequest(now, request) == 0 && run.screen == nbt::gui::SelfTestAsk);
    v = view(&run);
    CHECK(v.itemCount == 2 && !strcmp(v.items[0], "Yes") && !strcmp(v.items[1], "No") &&
          view_contains(&run, "red, green and blue bars"));
    press(&run, nbt::gui::KeySelect, 1);
    CHECK(view_contains(&run, "Did you hear three beeps?"));
    press(&run, nbt::gui::KeyDown, 1);
    press(&run, nbt::gui::KeySelect, 1);
    exchange(&run, "SELFTEST REPORT y n",
             "SELFTEST\t20\t2\t1\tyou.sound|f|no\tguest.start|f|no report"
             "\tmodule.diag|s|no gateway");
    v = view(&run);
    CHECK(run.screen == nbt::gui::SelfTestResults && v.itemCount == 4 &&
          !strcmp(v.items[0], "Failed: your answer, sound (no)") &&
          !strcmp(v.items[1], "Failed: guest, start (no report)") &&
          !strcmp(v.items[2], "Skipped: module, diagnostics (no gateway)") &&
          !strcmp(v.items[3], "OK"));
    CHECK(view_contains(&run, "20 passed, 2 failed") &&
          view_contains(&run, "1 skipped: the car or the answer did not allow them"));
    CHECK(!strcmp(nbt::gui::screenName(run.screen), "self-test-results"));
    press(&run, nbt::gui::KeyBack, 1);
    CHECK(run.screen == nbt::gui::SelfTest && view(&run).itemCount == 2);
    /* One never seen running within 15 s: stopped, the panel taken back. */
    UiController slow;
    loaded(&slow, "APPS\t1\tdemo", "RUNNING\t0");
    press(&slow, nbt::gui::KeyDown, 1);
    press(&slow, nbt::gui::KeySelect, 1);
    exchange(&slow, "SETTINGS manager live", MANAGER_SETTINGS);
    slow.focus = slow.managerEntry(nbt::gui::EntrySelfTest);
    press(&slow, nbt::gui::KeySelect, 1);
    exchange(&slow, "SELFTEST LAST", "SELFTEST\tnone");
    press(&slow, nbt::gui::KeySelect, 1);
    exchange(&slow, "SELFTEST PREPARE", "SELFTEST\tready\tmanager-selftest-native");
    now += nbt::gui::kPollMs;
    exchange(&slow, "START manager-selftest-native", "OK screen");
    CHECK(slow.takeHandover());
    now += 16000;
    CHECK(slow.nextRequest(now, request) == 0 && slow.takeAppEnded());
    exchange(&slow, "STOP manager-selftest-native", "OK");
    now += nbt::gui::kPollMs;
    exchange(&slow, "RUNNING status", "RUNNING\t0");
    now += nbt::gui::kPollMs;
    CHECK(slow.nextRequest(now, request) == 0 && slow.screen == nbt::gui::SelfTestAsk);
    /* Malformed reports are refused whole. */
    nbt::gui::SelfTestState state = nbt::gui::SelfTestState();
    CHECK(!nbt::gui::selfTestParseReport(state, "SELFTEST\t1\t2"));
    CHECK(!nbt::gui::selfTestParseReport(state, "SELFTEST\t1\t2\t3\tnodot|f|x"));
    CHECK(!nbt::gui::selfTestParseReport(state, "SELFTEST\t1\t2\t3\ta.b|p|x"));
    CHECK(!nbt::gui::selfTestParseReport(state, "SELFTEST\t1\t2\t3\ta.b|f|\x01"));
    CHECK(!nbt::gui::selfTestParseReport(state, "SELFTEST\t1000\t2\t3"));
    CHECK(!nbt::gui::selfTestParseReady(state, "SELFTEST\tready\tdemo"));
    CHECK(!nbt::gui::selfTestParseReady(
        state, "SELFTEST\tready\tmanager-a,manager-b,manager-c,manager-d"));
    CHECK(nbt::gui::selfTestParseReady(state, "SELFTEST\tready\t") && !state.count);
}

/* Settings > Tools: restart the head unit, which asks first and sends the
 * existing IDRIVE RESTART, and Enable SSH, which asks and sends SSH ENABLE
 * (tools_ui.hpp). Both come back to Tools. */
static void test_tools_menu(void) {
    UiController ui;
    Request request;
    loaded(&ui, "APPS\t1\tdemo", "RUNNING\t0");
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    CHECK(ui.managerEntry(nbt::gui::EntryTools) == 8); // after Status and Self test
    ui.focus = ui.managerEntry(nbt::gui::EntryTools);
    press(&ui, nbt::gui::KeySelect, 1);
    UiView v = view(&ui);
    CHECK(ui.screen == nbt::gui::Tools && !strcmp(v.title, "Tools") && v.itemCount == 2 &&
          !strcmp(v.items[0], "Restart head unit") && !strcmp(v.items[1], "Enable SSH"));
    CHECK(v.itemStyle[0] == nbt::gui::ItemAction && v.itemStyle[1] == nbt::gui::ItemAction);
    CHECK(!strcmp(nbt::gui::screenName(ui.screen), "tools"));
    /* BACK returns to Settings, on Tools. */
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == ui.managerEntry(nbt::gui::EntryTools));
    /* Restart asks first, without the plugin update's lines, and Later comes back. */
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::RestartIdrive && !strcmp(v.title, "Restart head unit?") &&
          v.focus == 0 && v.itemCount == 2 && !strcmp(v.items[0], "Later") &&
          !strcmp(v.items[1], "Restart head unit"));
    CHECK(v.lineCount == 1 && !strcmp(v.lines[0], "The whole system restarts, as car off/on"));
    press(&ui, nbt::gui::KeySelect, 1); /* Later */
    CHECK(ui.screen == nbt::gui::Tools && ui.focus == 0 && !ui.queued);
    /* Restart head unit sends the one restart request, the whole system's. */
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "IDRIVE RESTART"));
    /* While it runs, the question keeps the Tools wording. */
    CHECK(ui.screen == nbt::gui::RestartIdrive && view(&ui).lineCount == 1);
    CHECK(ui.reply("OK", now) == 0 && ui.screen == nbt::gui::Result &&
          !strcmp(view(&ui).title, "Tools") && view_contains(&ui, "Restarting the head unit"));
    CHECK(!ui.restartFromTools);
    /* Back to the list, then Settings > Tools > Enable SSH: asked first. */
    press(&ui, nbt::gui::KeySelect, 1); /* OK */
    exchange(&ui, "LIST", "APPS\t1\tdemo");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 1); /* Settings, as after the result */
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    ui.focus = ui.managerEntry(nbt::gui::EntryTools);
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::SshConfirm && !strcmp(v.title, "Enable SSH?") && v.focus == 0 &&
          !strcmp(v.items[0], "No") && !strcmp(v.items[1], "Enable SSH"));
    CHECK(view_contains(&ui, "SSH, Telnet and qconn open as root to the car's network") &&
          view_contains(&ui, "On until the head unit restarts"));
    CHECK(!strcmp(nbt::gui::screenName(ui.screen), "ssh-confirm"));
    press(&ui, nbt::gui::KeySelect, 1); /* No */
    CHECK(ui.screen == nbt::gui::Tools && ui.focus == 1 && !ui.queued);
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "SSH ENABLE"));
    CHECK(ui.reply("OK", now) == 0 && ui.screen == nbt::gui::Result &&
          !strcmp(view(&ui).title, "Tools") && view_contains(&ui, "SSH enabled") &&
          view_contains(&ui, "On until the head unit restarts"));
    /* A refusal says so. */
    ui.go(nbt::gui::SshConfirm);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "SSH ENABLE"));
    CHECK(ui.reply("ERR ssh", now) == 0 && ui.screen == nbt::gui::Result &&
          view_contains(&ui, "Cannot enable SSH"));
}

/* A manager update from a phone: named, no "Install, even if older". */
static void test_manager_update_review(void) {
    UiController ui;
    loaded(&ui, "APPS\t1\ttest", "RUNNING\t0");
    snprintf(ui.review.app, sizeof(ui.review.app), "manager");
    snprintf(ui.review.installed, sizeof(ui.review.installed), "v2");
    snprintf(ui.review.proposed, sizeof(ui.review.proposed), "v3");
    ui.go(nbt::gui::Review);
    UiView v = view(&ui);
    CHECK(!strcmp(v.title, "Review update") && view_contains(&ui, "NBT Manager") &&
          view_contains(&ui, "Installed: v2, new: v3"));
    CHECK(v.itemCount == 2 && ui.itemCount() == 2 && !strcmp(v.items[0], "Reject") &&
          !strcmp(v.items[1], "Install") && v.focus == 0);
}

static void test_pairing_timeout(void) {
    UiController ui;
    loaded(&ui, "APPS\t1\ttest", "RUNNING\t0");
    pair_from_settings(&ui, "SESSION\t172.16.222.1:8080\tDIRECT-03-BMW\tpassphrase");
    exchange(&ui, "PHONE OPEN", "OK");
    CHECK(view_contains(&ui, "Closes in 60 s"));
    /* The once-a-second polls do not show the busy dots (seen in the car, dev43). */
    Request request;
    now += nbt::gui::kPollMs;
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "STATUS"));
    CHECK(!view(&ui).busy);
    CHECK(ui.reply("STATUS\t1\t-\t-\t-\t0\t-\tReady for upload", now) == 0);
    CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "PHONE STATUS"));
    CHECK(!view(&ui).busy);
    CHECK(ui.reply("PHONE\tnone", now) == 0 && ui.screen == nbt::gui::Pairing);
    now += nbt::gui::kPairingMs;
    exchange(&ui, "CANCEL", "OK");
    CHECK(ui.screen == nbt::gui::Apps && !ui.hasWifi() && !ui.host[0]);
    /* Without the car's Wi-Fi details there is no code to show. */
    UiController none;
    loaded(&none, "APPS\t1\ttest", "RUNNING\t0");
    pair_from_settings(&none, "SESSION\t127.0.0.1:4567");
    CHECK(none.screen == nbt::gui::Result && view_contains(&none, "Cannot pair a phone"));
    exchange(&none, "CANCEL", "OK");
}

static void expect_fatal(const char *list_reply, const char *running_reply,
                         const char *session_reply, const char *status_reply) {
    UiController ui;
    char command[nbt::gui::kCommandMax];
    unsigned timeout;
    CHECK(initUi(&ui, "lo", 0) == 0);
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 1);
    if (list_reply) {
        CHECK(replyUi(&ui, list_reply, now) == -1);
        goto fatal;
    }
    CHECK(replyUi(&ui, "APPS\t2\tdemo\ttest", now) == 0);
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 1);
    if (running_reply) {
        CHECK(replyUi(&ui, running_reply, now) == -1);
        goto fatal;
    }
    CHECK(replyUi(&ui, "RUNNING\t0", now) == 0);
    ui.queued = nbt::gui::R_SESSION; /* as open_session */
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 1);
    if (session_reply) {
        CHECK(replyUi(&ui, session_reply, now) == -1);
        goto fatal;
    }
    CHECK(replyUi(&ui, "SESSION\t127.0.0.1:1", now) == 0);
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 1);
    CHECK(replyUi(&ui, status_reply, now) == -1);
fatal:
    CHECK(ui.screen == nbt::gui::Error && ui.fatal && view_contains(&ui, "protocol"));
    CHECK(!ui.hasWifi());
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 0);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(wantsExit(&ui));
}

static void test_malformed_replies(void) {
    static const char *lists[] = {"APPS\t2\ttest",
                                  "APPS\t1\ttest\tmore",
                                  "APPS\t2\ttest\ttest",
                                  "APPS\t1\tTest",
                                  "APPS\t1\t../x",
                                  "APPS\t17",
                                  "APPS\t01\ttest",
                                  "APPS\t\ttest",
                                  "APPS",
                                  "APPS\t-1",
                                  "APPS\t1\t",
                                  "OK",
                                  "ERR",
                                  "ERR Bad",
                                  "APPS\t99999999999999999999",
                                  "APPS\t1\ttest\t"};
    static const char *runnings[] = {"RUNNING\t1\tother",
                                     "RUNNING\t2\ttest\ttest",
                                     "RUNNING\t1",
                                     "RUNNING\t0\ttest",
                                     "RUNNING",
                                     "RUNNING\t3\tdemo\ttest\tx",
                                     "RUNNING\t1\tTest",
                                     "APPS\t0",
                                     "OK",
                                     "RUNNING\t1\ttest\t",
                                     "RUNNING\t1\ttest|",
                                     "RUNNING\t1\ttest|abcdefghijklmnopqrstuvwxy",
                                     "RUNNING\t1\ttest|bad\n",
                                     "RUNNING\t1\ttest|bad\x7f"};
    static const char *sessions[] = {"SESSION\t127.0.0.1",
                                     "SESSION\t127.0.0.1:65536",
                                     "SESSION\t256.0.0.1:1",
                                     "SESSION\t1.2.3:4",
                                     "SESSION\t1.2.3.4.5:4",
                                     "SESSION\t1..3.4:4",
                                     "SESSION\t1.2.3.4:4:5",
                                     "SESSION\t1.2.3.4:4\tlegacy-token",
                                     "SESSION\t1.2.3.4:4\tDIRECT-03-X\tshort",
                                     "OK"};
    static const char *statuses[] = {"STATUS\t7\ttest\tv1\t-\t0\t-\tx",
                                     "STATUS\t6\ttest\tv1\tv2\t2\t" DIGEST_A "\tx",
                                     "STATUS\t2\ttest\tv1\tv2\t2\t-\tx",
                                     "STATUS\t2\ttest\tv1\t-\t2\t" DIGEST_A "\tx",
                                     "STATUS\t1\ttest\tv1\tv2\t0\t-\tx",
                                     "STATUS\t1\ttest\tv1\t-\t3\t-\tx",
                                     "STATUS\t1\ttest\tv1\t-\t0\t" DIGEST_A "\tx",
                                     "STATUS\t2\ttest\tv1\tv2\t2\t" DIGEST_A "0\tx",
                                     "STATUS\t2\ttest\tv1\tv2\t02\t" DIGEST_A "\tx",
                                     "STATUS\t2\t-\tv1\tv2\t2\t" DIGEST_A "\tx",
                                     "STATUS\t1\tte st\tv1\t-\t0\t-\tx",
                                     "STATUS\t1\ttest\tv1\t-\t0\t-",
                                     "STATUS\t1\ttest\tv1\t-\t0\t-\tx\ty\tz",
                                     "STATUS\t1\ttest\tv1\t-\t0\t-\tx\t",
                                     "STATUS\t1\ttest\tv1\t-\t0\t-\tx\t Alice",
                                     "STATUS\t1\ttest\tv1\t-\t0\t-\tx\tA|B",
                                     "STATUS\t1\ttest\tv1\t-\t0\t-\t",
                                     "STATUS\t1\ttest\tv/1\t-\t0\t-\tx",
                                     "STATUS\t11\ttest\tv1\t-\t0\t-\tx",
                                     "STATUS\t2\ttest\tv1\tv2\t18446744073709551616\t" DIGEST_A
                                     "\tx",
                                     "OK"};
    size_t i;
    for (i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
        expect_fatal(lists[i], NULL, NULL, NULL);
    }
    for (i = 0; i < sizeof(runnings) / sizeof(runnings[0]); ++i) {
        expect_fatal(NULL, runnings[i], NULL, NULL);
    }
    for (i = 0; i < sizeof(sessions) / sizeof(sessions[0]); ++i) {
        expect_fatal(NULL, NULL, sessions[i], NULL);
    }
    for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        expect_fatal(NULL, NULL, NULL, statuses[i]);
    }
}

static void test_errors_and_misuse(void) {
    UiController ui;
    WorkerStatus status;
    char command[nbt::gui::kCommandMax], long_reply[1100];
    unsigned timeout;
    CHECK(initUi(NULL, "lo", 0) == -1);
    CHECK(initUi(&ui, "bad if", 0) == -1);
    CHECK(initUi(&ui, "averyveryverylongif", 0) == -1);
    CHECK(initUi(&ui, "lo", 65536) == -1);
    CHECK(initUi(&ui, "lo", 0) == 0);
    /* A host notice is a dialog on the main menu until closed. */
    setNotice(&ui, "Controller config invalid; defaults used");
    exchange(&ui, "LIST", "APPS\t1\ttest");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(view_contains(&ui, "Controller config invalid") && view(&ui).modal.shown &&
          view(&ui).lineCount == 1);
    press(&ui, nbt::gui::KeySelect, 1); /* OK */
    CHECK(!view(&ui).modal.shown && !ui.notice[0]);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    CHECK(ui.screen == nbt::gui::Settings && !view_contains(&ui, "Controller config"));
    press(&ui, nbt::gui::KeyBack, 1);
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(ui.screen == nbt::gui::Apps && !view_contains(&ui, "Controller config"));
    CHECK(initUi(&ui, "lo", 0) == 0);
    CHECK(replyUi(&ui, "APPS\t0", now) == -1); /* nothing outstanding */
    CHECK(nextRequest(&ui, now, command, 4, &timeout) == -1);
    exchange(&ui, "LIST", "ERR list");
    CHECK(ui.screen == nbt::gui::Error && !ui.fatal && view_contains(&ui, "Retry") &&
          !strcmp(view(&ui).title, "Error"));
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "LIST", "APPS\t0");
    exchange(&ui, "RUNNING status", "ERR list");
    CHECK(ui.screen == nbt::gui::Error);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "LIST", "APPS\t0");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(view_contains(&ui, "No applications") && view(&ui).itemCount == 1);
    CHECK(initUi(&ui, "lo", 0) == 0);
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 1);
    requestFailed(&ui);
    CHECK(ui.screen == nbt::gui::Error && ui.fatal && view_contains(&ui, "unavailable"));
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 0);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(wantsExit(&ui) && wantsExit(NULL));
    memset(long_reply, 'a', sizeof(long_reply) - 1);
    long_reply[sizeof(long_reply) - 1] = 0;
    memcpy(long_reply, "STATUS\t1\t", 9);
    CHECK(UiController::parseStatus(long_reply, status) == -1);
    CHECK(UiController::parseStatus("STATUS\t0\t-\t-\t-\t0\t-\t-", status) == 0 &&
          !status.message[0] && !status.app[0]);
    // The developer key's name, when a developer key signed the package.
    CHECK(UiController::parseStatus("STATUS\t2\ttest\tv1\tv2\t2\t" DIGEST_A
                                    "\tPackage verified; approve in iDrive\tAlice Dev",
                                    status) == 0 &&
          !strcmp(status.developer, "Alice Dev"));
    CHECK(UiController::parseStatus("STATUS\t0\t-\t-\t-\t0\t-\tIdle\t-", status) == 0 &&
          !status.developer[0] && !status.permissions);
    // Its permissions, and those an update adds (docs/permissions.md).
    CHECK(UiController::parseStatus("STATUS\t2\ttest\tv1\tv2\t2\t" DIGEST_A
                                    "\tPackage verified; approve in iDrive\t-\tperm:304:200",
                                    status) == 0 &&
          status.permissions == 0x304 && status.newPermissions == 0x200 && !status.developer[0]);
    const char *badPermissions[] = {"clear-codes", "perm:304:800", "perm:1fff:0",
                                    "perm:4:4x",   "perm:4",       "perm::"};
    for (const char *bad : badPermissions) {
        char reply[256];
        snprintf(reply, sizeof(reply),
                 "STATUS\t2\ttest\tv1\tv2\t2\t" DIGEST_A
                 "\tPackage verified; approve in iDrive\tAlice\t%s",
                 bad);
        CHECK(UiController::parseStatus(reply, status) == -1);
    }
    CHECK(-1 == -1);
}

static void test_app_settings(void) {
    UiController ui;
    UiView v;
    loaded(&ui, "APPS\t2\tdemo\ttest", "RUNNING\t0");
    press(&ui, nbt::gui::KeyDown, 1);
    options(&ui, "SETTINGS test live",
            "SETTINGS\t2\trestart\tbackground|Background color|blue|blue,red,green,black"
            "\tsize|Text size|small|small,large");
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::Settings && !strcmp(v.title, "test settings") && v.itemCount == 5);
    CHECK(!strcmp(v.items[0], "Background color: blue") &&
          !strcmp(v.items[1], "Text size: small") &&
          !strcmp(v.items[2], "Start with the car: off") &&
          !strcmp(v.items[3], "App shortcut: off") && !strcmp(v.items[4], "Uninstall test"));
    press(&ui, nbt::gui::KeySelect, 1);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::Choice && !strcmp(v.title, "Background color") &&
          v.itemCount == 4 && v.focus == 0);
    CHECK(!strcmp(v.items[0], "blue  (current)") && !strcmp(v.items[1], "red") &&
          view_contains(&ui, "test settings"));
    /* BACK leaves the choice unchanged. */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == 0 && !ui.queued);
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeySelect, 0); /* busy while saving */
    exchange(&ui, "SET test background red", "OK");
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == 0);
    exchange(&ui, "SETTINGS test live",
             "SETTINGS\t2\trestart\tbackground|Background color|red|blue,red,green,black"
             "\tsize|Text size|small|small,large");
    CHECK(!strcmp(view(&ui).items[0], "Background color: red"));
    /* The second setting opens on its current value; a refused save says so. */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Choice && ui.focus == 0 && !strcmp(view(&ui).title, "Text size"));
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SET test size large", "ERR settings");
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == 1 &&
          view_contains(&ui, "Cannot save the setting"));
    /* Settings that can no longer be read return to the main menu. */
    exchange(&ui, "SETTINGS test live", "ERR settings");
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 1 &&
          view_contains(&ui, "Cannot read test settings"));
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    press(&ui, nbt::gui::KeySelect, 1); /* OK closes its dialog */
    /* The manager's own settings, through the worker's reserved name. */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(!view_contains(&ui, "Cannot read"));
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::Settings && !strcmp(v.title, "Settings") && v.itemCount == 9);
    CHECK(!strcmp(v.items[0], "Debug log: off") &&
          !strcmp(v.items[2], "Hand back controller: off"));
    CHECK(view_contains(&ui, "Changes apply when the manager next opens"));
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Choice && !strcmp(view(&ui).title, "Debug log") &&
          !strcmp(view(&ui).lines[0], "Settings"));
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SET manager debug_log on", "OK");
    exchange(&ui, "SETTINGS manager live",
             "SETTINGS\t4\trestart\tdebug_log|Debug log|on|off,on\tinvert|Invert knob|off|off,on"
             "\trelease|Hand back controller|off|off,on"
             "\tnative_audio|Native audio|media|interrupt,media");
    CHECK(!strcmp(view(&ui).items[0], "Debug log: on") &&
          !strcmp(view(&ui).items[3], "Native audio: Media source"));
    /* BACK returns to the main menu, on Settings. */
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Apps && ui.focus == 2);
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    /* Without applications, Settings still has the manager's. */
    loaded(&ui, "APPS\t0", "RUNNING\t0");
    press(&ui, nbt::gui::KeySelect, 1); /* Settings comes first */
    exchange(&ui, "SETTINGS manager live", MANAGER_SETTINGS);
    CHECK(ui.screen == nbt::gui::Settings && view(&ui).itemCount == 9);
    /* Full declarations exceed the old 1024-byte limit; all twelve settings
     * remain navigable, followed by Stop and Uninstall for a running app. */
    {
        char reply[nbt::gui::kReplyMax];
        size_t i, used = (size_t)snprintf(reply, sizeof(reply), "SETTINGS\t12\tlive");
        for (i = 0; i < 12; ++i) {
            int n = snprintf(reply + used, sizeof(reply) - used,
                             "\tkey%013u|Label with twenty "
                             "chars|abcdefghijkl|abcdefghijkl,bcdefghijklm,cdefghijklmn,"
                             "defghijklmno,efghijklmnop,fghijklmnopq",
                             (unsigned)i);
            CHECK(n > 0 && (size_t)n < sizeof(reply) - used);
            used += (size_t)n;
        }
        CHECK(used > 1024);
        loaded(&ui, "APPS\t1\ttest", "RUNNING\t1\ttest");
        options(&ui, "SETTINGS test live", reply);
        CHECK(ui.settings.count == 12 && view(&ui).itemCount == 16);
        CHECK(view_contains(&ui, "Changes apply at once"));
        for (i = 0; i < 11; ++i) {
            press(&ui, nbt::gui::KeyDown, 1);
        }
        press(&ui, nbt::gui::KeySelect, 1);
        CHECK(ui.screen == nbt::gui::Choice && view(&ui).itemCount == 6);
        press(&ui, nbt::gui::KeyDown, 1);
        press(&ui, nbt::gui::KeySelect, 1);
        exchange(&ui, "SET test key0000000000011 bcdefghijklm", "OK");
        exchange(&ui, "SETTINGS test live", reply);
        CHECK(ui.screen == nbt::gui::Settings && ui.focus == 11);
    }
    /* Malformed replies are protocol errors. */
    {
        static const char *const bad[] = {
            "SETTINGS\t1\trestart",
            "SETTINGS\t13\trestart",
            "SETTINGS\t0\tunknown",
            "SETTINGS\t-1\tlive",
            "SETTINGS\t99999999\tlive",
            "SETTINGS\t1\trestart\tbackground|Background|pink|blue,red",
            "SETTINGS\t2\trestart\tx|A|a|a\tx|B|b|b",
            "SETTINGS\t1\trestart\tBad|A|a|a",
            "SETTINGS\t1\trestart\tx|A|a|a|b",
            "SETTINGS\t1\trestart\tx||a|a",
            "SETTINGS\t1\trestart\tx|A|a|a,a",
            "SETTINGS\t1\trestart\tx|A|a|a,b,c,d,e,f,g",
            "SETTINGS\t1\trestart\tx|A|a|",
            "SETTINGS\t1\trestart\tx|A|a|toolongchoice1",
            "SETTINGS\t01\trestart\tx|A|a|a",
            "SETTINGS\t0\trestart+bogus",
            "SETTINGS\t0\t+autostart",
            "SETTINGS\t0\tautostart+live",
            "OK"};
        size_t i;
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            loaded(&ui, "APPS\t1\ttest", "RUNNING\t0");
            press(&ui, nbt::gui::KeyOption, 1);
            {
                char command[nbt::gui::kCommandMax];
                unsigned timeout;
                CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 1 &&
                      !strcmp(command, "SETTINGS test live"));
                if (replyUi(&ui, bad[i], now) != -1) {
                    fprintf(stderr, "accepted: %s\n", bad[i]);
                }
            }
            CHECK(ui.screen == nbt::gui::Error && ui.fatal);
        }
    }
}

static uint32_t pixel(const Frame *f, size_t x, size_t y) {
    uint32_t value;
    memcpy(&value, f->pixels + y * f->stride + x * 4, sizeof(value));
    return value;
}

/* The scroll bar column of a frame (16 px margin, 6 px bar): the topmost and
 * bottom-most rows of the thumb, and whether its track is there at all. */
static int scrollbar(const Frame *f, size_t *top, size_t *bottom) {
    size_t x = f->width - 16 - 3, y;
    int track = 0;
    *top = *bottom = 0;
    for (y = 0; y < f->height; ++y) {
        uint32_t c = pixel(f, x, y) & 0xffffffu;
        if (c == nbt::gui::palette(0).track) {
            track = 1;
        }
        if (c == nbt::gui::palette(0).muted) {
            if (!*bottom) {
                *top = y;
            }
            *bottom = y + 1;
            track = 1;
        }
    }
    return track;
}

static void test_rendering(void) {
    enum { W = 640, H = 240, GUARD = 64 };
    static unsigned char memory[GUARD + W * H * 4 + GUARD];
    Frame frame;
    UiController ui;
    UiView v;
    size_t i;
    char c[2] = {0, 0};
    memset(memory, 0xa5, sizeof(memory));
    frame.pixels = memory + GUARD;
    frame.bytes = W * H * 4;
    frame.width = W;
    frame.height = H;
    frame.stride = W * 4;
    for (i = 1; i < 256; ++i) {
        c[0] = (char)i;
        CHECK(frame.text(c, 0, 0, 1, 0xffffffffu).ok());
    }
    verified_review(&ui, DIGEST_A);
    CHECK(viewGet(&ui, &v) == 0 && renderUi(&v, NULL, &frame) == 0);
    CHECK(initUi(&ui, "lo", 0) == 0);
    exchange(&ui, "LIST",
             "APPS\t16\ta0\ta1\ta2\ta3\ta4\ta5\ta6\ta7\ta8\ta9\tb0\tb1\tb2\tb3\tb4"
             "\tb5555555555555555555555555555555555555555555555");
    exchange(&ui, "RUNNING status",
             "RUNNING\t16\ta0\ta1\ta2\ta3\ta4\ta5\ta6\ta7\ta8\ta9\tb0\tb1\tb2\tb3\tb4"
             "\tb5555555555555555555555555555555555555555555555");
    {
        /* 17 items do not fit in 240 px: a scroll bar, whose thumb follows the
         * focus; going back up one item from the end does not move the list. */
        size_t top, bottom, top0, bottom0;
        CHECK(viewGet(&ui, &v) == 0 && renderUi(&v, NULL, &frame) == 0);
        CHECK(scrollbar(&frame, &top0, &bottom0) && bottom0 > top0);
        for (i = 0; i < 20; ++i) {
            pressUi(&ui, nbt::gui::KeyDown);
        }
        CHECK(ui.focus == 16);
        CHECK(viewGet(&ui, &v) == 0 && v.itemCount == 17 && renderUi(&v, NULL, &frame) == 0);
        CHECK(scrollbar(&frame, &top, &bottom) && top > top0 && bottom > bottom0);
        pressUi(&ui, nbt::gui::KeyUp);
        CHECK(viewGet(&ui, &v) == 0 && renderUi(&v, NULL, &frame) == 0);
        CHECK(scrollbar(&frame, &top0, &bottom0) && top0 == top && bottom0 == bottom);
        for (i = 0; i < 20; ++i) {
            pressUi(&ui, nbt::gui::KeyUp);
        }
        CHECK(viewGet(&ui, &v) == 0 && renderUi(&v, NULL, &frame) == 0);
        CHECK(scrollbar(&frame, &top, &bottom) && top < top0);
        pressUi(&ui, nbt::gui::KeyDown);
        CHECK(viewGet(&ui, &v) == 0 && renderUi(&v, NULL, &frame) == 0);
        CHECK(scrollbar(&frame, &top0, &bottom0) && top0 == top);
        /* A list that fits has none. */
        v.itemCount = 3;
        v.focus = 2;
        CHECK(renderUi(&v, NULL, &frame) == 0 && !scrollbar(&frame, &top, &bottom));
        v.itemCount = 17;
        v.focus = 16;
    }
    v.focus = 17;
    CHECK(renderUi(&v, NULL, &frame) == -1);
    /* The pairing code: drawn large on its panel. */
    v.focus = 0;
    strcpy(v.code, "123 456");
    CHECK(renderUi(&v, NULL, &frame) == 0);
    v.code[0] = 0;
    /* The hold progress bar. */
    v.focus = 0;
    v.progress = 0;
    CHECK(renderUi(&v, NULL, &frame) == 0);
    v.progress = 100;
    CHECK(renderUi(&v, NULL, &frame) == 0);
    v.progress = 101;
    CHECK(renderUi(&v, NULL, &frame) == -1);
    v.progress = -2;
    CHECK(renderUi(&v, NULL, &frame) == -1);
    v.progress = -1;
    /* A dialog over the list: a bar, or two choices. */
    v.modal.shown = 1;
    strcpy(v.modal.title, "Stop a0?");
    strcpy(v.modal.text, "Keep holding to stop it");
    v.modal.progress = 30;
    CHECK(renderUi(&v, NULL, &frame) == 0);
    v.modal.progress = -1;
    v.modal.itemCount = 2;
    strcpy(v.modal.items[0], "No");
    strcpy(v.modal.items[1], "Yes, stop");
    v.modal.focus = 1;
    CHECK(renderUi(&v, NULL, &frame) == 0);
    v.modal.focus = 2;
    CHECK(renderUi(&v, NULL, &frame) == -1);
    v.modal.focus = 0;
    v.modal.itemCount = 3;
    CHECK(renderUi(&v, NULL, &frame) == -1);
    v.modal.itemCount = 2;
    v.modal.progress = 101;
    CHECK(renderUi(&v, NULL, &frame) == -1);
    v.modal = Modal();
    v = UiView();
    memset(v.title, 'W', nbt::gui::kTextMax);
    for (i = 0; i < nbt::gui::kLines; ++i) {
        memset(v.lines[i], 'M', nbt::gui::kTextMax);
    }
    v.lineCount = nbt::gui::kLines;
    v.itemCount = nbt::gui::kItems;
    v.focus = nbt::gui::kItems - 1;
    v.busy = 1;
    v.progress = 40;
    for (i = 0; i < nbt::gui::kItems; ++i) {
        memset(v.items[i], 'X', nbt::gui::kTextMax);
    }
    CHECK(renderUi(&v, NULL, &frame) == 0);
    /* The largest dialog, also on the small frames below. */
    v.modal.shown = 1;
    memset(v.modal.title, 'W', nbt::gui::kTextMax);
    memset(v.modal.text, 'M', nbt::gui::kTextMax);
    v.modal.progress = 100;
    v.modal.itemCount = nbt::gui::kModalItems;
    v.modal.focus = nbt::gui::kModalItems - 1;
    for (i = 0; i < nbt::gui::kModalItems; ++i) {
        memset(v.modal.items[i], 'X', nbt::gui::kTextMax);
    }
    CHECK(renderUi(&v, NULL, &frame) == 0);
    frame.width = 40;
    frame.height = 30;
    frame.stride = 40 * 4;
    CHECK(renderUi(&v, NULL, &frame) == 0);
    frame.width = 1;
    frame.height = 1;
    frame.stride = 4;
    CHECK(renderUi(&v, NULL, &frame) == 0);
    frame.bytes = 3;
    CHECK(renderUi(&v, NULL, &frame) == -1);
    for (i = 0; i < GUARD; ++i) {
        CHECK(memory[i] == 0xa5 && memory[sizeof(memory) - 1 - i] == 0xa5);
    }
}

static void test_pairing_qr(void) {
    enum { W = 640, H = 240 };
    static unsigned char memory[W * H * 4];
    Frame frame = {memory, sizeof(memory), W, H, W * 4};
    UiController ui;
    UiView v;
    QrCode qr, empty;
    char link[160];
    size_t side, scale, qx, qy;
    CHECK(initUi(&ui, "uap0", 8080) == 0);
    exchange(&ui, "LIST", "APPS\t1\ttest");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    open_session(&ui, "SESSION uap0 8080",
                 "SESSION\t172.16.224.1:8080\tDIRECT-03-BMW34567\tpassphrase");
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "PHONE OPEN", "OK");
    CHECK(pairingLink(&ui, link, sizeof(link)) == 0);
    CHECK(encodeQr(qr, (const unsigned char *)link, strlen(link), -1) == 0 && qr.version_ >= 1);
    CHECK(viewGet(&ui, &v) == 0 && renderUi(&v, &qr, &frame) == 0);
    side = qr.size() + 2 * nbt::gui::kQrQuiet;
    scale = H / side;
    qx = W - side * scale - 8;
    qy = (H - side * scale) / 2;
    CHECK(scale >= 4);
    CHECK(pixel(&frame, qx, qy) == 0xffffffffu); /* quiet zone */
    CHECK(pixel(&frame, qx + nbt::gui::kQrQuiet * scale, qy + nbt::gui::kQrQuiet * scale) ==
          0xff000000u);
    CHECK(pixel(&frame, qx + (nbt::gui::kQrQuiet + 1) * scale,
                qy + (nbt::gui::kQrQuiet + 1) * scale) == 0xffffffffu);
    CHECK(pixel(&frame, qx - 1, qy) != 0xffffffffu); /* text area */
    /* Too small for a scannable code: the view is drawn without it. */
    frame.width = 100;
    frame.height = 60;
    frame.stride = 100 * 4;
    CHECK(renderUi(&v, &qr, &frame) == 0 && pixel(&frame, 99, 30) != 0xffffffffu);
    empty.clear();
    CHECK(renderUi(&v, &empty, &frame) == -1);
    CHECK(!qr.render(frame, 0, 0, 2).ok());
    qr.clear();
    CHECK(qr.empty());
}

static void test_application_status(void) {
    UiController ui;
    char command[nbt::gui::kCommandMax], reply[nbt::gui::kReplyMax], apps[nbt::gui::kReplyMax];
    unsigned timeout;
    size_t i, used, app_used;
    loaded(&ui, "APPS\t2\tdemo\ttest", "RUNNING\t2\tdemo\ttest|Connecting");
    CHECK(!strcmp(view(&ui).items[0], "demo  (running)"));
    CHECK(!strcmp(view(&ui).items[1], "test  (Connecting)"));
    idle(&ui);
    now += nbt::gui::kPollMs;
    exchange(&ui, "RUNNING status", "RUNNING\t1\ttest|Connected | ready");
    CHECK(!strcmp(view(&ui).items[0], "demo"));
    CHECK(!strcmp(view(&ui).items[1], "test  (Connected | ready)"));
    now += nbt::gui::kPollMs;
    CHECK(nextRequest(&ui, now, command, sizeof(command), &timeout) == 1);
    CHECK(!strcmp(command, "RUNNING status") && !view(&ui).busy);
    /* A push/release during the refresh queues the start, without loss. */
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 1);
    CHECK(replyUi(&ui, "RUNNING\t1\ttest", now) == 0);
    CHECK(!strcmp(view(&ui).items[1], "test  (running)"));
    exchange(&ui, "START demo", "ERR start");
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(!strcmp(view(&ui).items[1], "test"));
    /* Full-size valid reply exceeds the former 1024-byte GUI reply buffer. */
    used = (size_t)snprintf(reply, sizeof(reply), "RUNNING\t16");
    app_used = (size_t)snprintf(apps, sizeof(apps), "APPS\t16");
    for (i = 0; i < 16; ++i) {
        char name[nbt::gui::kIdMax + 1];
        memset(name, 'a', nbt::gui::kIdMax);
        name[nbt::gui::kIdMax] = 0;
        name[0] = (char)('a' + i);
        app_used += (size_t)snprintf(apps + app_used, sizeof(apps) - app_used, "\t%s", name);
        used += (size_t)snprintf(reply + used, sizeof(reply) - used,
                                 "\t%s|abcdefghijklmnopqrstuvwx", name);
    }
    CHECK(used > 1024 && used < sizeof(reply));
    loaded(&ui, apps, reply);
    // The longest name with the longest status fits whole.
    CHECK(strlen(view(&ui).items[0]) == nbt::gui::kIdMax + nbt::gui::kAppStatusMax + 4);
    CHECK(strstr(view(&ui).items[15], "abcdefghijklmnopqrstuvwx)"));
}

static void test_quarantine_unblock_and_abort_app(void) {
    UiController ui;
    loaded(&ui, "APPS\t1\ttest", "RUNNING\t0");
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(releaseUi(&ui, nbt::gui::KeySelect) == 1);
    exchange(&ui, "START test", "ERR quarantined");
    CHECK(view_contains(&ui, "test blocked after faults"));
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    press(&ui, nbt::gui::KeySelect, 1); /* OK closes its dialog */

    /* OPTION opens Settings with Unblock test before Uninstall test (after Start with the car). */
    options(&ui, "SETTINGS test live", "SETTINGS\t0\tlive");
    CHECK(ui.screen == nbt::gui::Settings && view(&ui).itemCount == 4);
    CHECK(!strcmp(view(&ui).items[2], "Unblock test") &&
          !strcmp(view(&ui).items[3], "Uninstall test"));
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "UNBLOCK test", "OK");
    CHECK(ui.screen == nbt::gui::Settings && view(&ui).itemCount == 3);
    CHECK(view_contains(&ui, "test unblocked"));

    /* abortApp stops a hung/failed screen app with STOP <app> fault and clears stopping on Apps. */
    ui.go(nbt::gui::Apps);
    strcpy(ui.target, "test");
    ui.abortApp(nbt::gui::T_STOPPING_APP);
    CHECK(!strcmp(view(&ui).items[0], "test  (stopping)") && !view(&ui).modal.shown);
    exchange(&ui, "STOP test fault", "OK");
    CHECK(ui.stopping == 1 && ui.screen == nbt::gui::Apps);
    exchange(&ui, "STATUS", "STATUS\t0\t-\t-\t-\t0\t-\tApplication stopped");
    CHECK(ui.stopping == 0 && !view_contains(&ui, "test stopped") && !view(&ui).modal.shown);
    exchange(&ui, "RUNNING status", "RUNNING\t0");
    CHECK(!strcmp(view(&ui).items[0], "test"));
}

/* Settings > Files: stored files by page, a deletion, a USB scan and import. */
#define FILES_PAGE_ONE                                                                             \
    "FILES\t13\t3145728\t1073741824\t5000000000\t1\tA.sfc\t1048576\tB.sfc\t10\tC.sfc\t10\tD."      \
    "sfc\t10"                                                                                      \
    "\tE.sfc\t10\tF.sfc\t10\tG.sfc\t10\tH.sfc\t10\tI.sfc\t10\tJ.sfc\t10\tK.sfc\t10\tL.sfc\t10"
/* An application's Start with the car: after its own settings, switched at once,
 * paused after failed automatic starts (PROTOCOL.md, "Autostart"). */
static void test_autostart_setting(void) {
    UiController ui;
    UiView v;
    loaded(&ui, "APPS\t1\tdemo", "RUNNING\t1\tdemo");
    options(&ui, "SETTINGS demo live", TEST_SETTINGS);
    v = view(&ui);
    CHECK(v.itemCount == 5 && !strcmp(v.items[1], "Start with the car: off") &&
          !strcmp(v.items[2], "App shortcut: off") && !strcmp(v.items[3], "Stop demo") &&
          !strcmp(v.items[4], "Uninstall demo"));
    CHECK(ui.autostartItem() == 1 && ui.shortcutItem() == 2 && ui.stopItem() == 3 &&
          ui.uninstallItem() == 4);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "AUTOSTART demo on", "OK");
    exchange(
        &ui, "SETTINGS demo live",
        "SETTINGS\t1\trestart+autostart\tbackground|Background color|blue|blue,red,green,black");
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == ui.autostartItem());
    CHECK(!strcmp(v.items[1], "Start with the car: on") && !view_contains(&ui, "Paused"));
    /* Paused: the page says how to resume; switching it off is still possible. */
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "AUTOSTART demo off", "OK");
    exchange(&ui, "SETTINGS demo live",
             "SETTINGS\t1\trestart+autostart-paused\tbackground|Background "
             "color|blue|blue,red,green,black");
    CHECK(!strcmp(view(&ui).items[1], "Start with the car: on"));
    CHECK(view_contains(&ui, "Paused after failed starts: start demo once from here"));
    /* A refused switch says so and shows the page as the worker has it. */
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "AUTOSTART demo off", "ERR settings");
    CHECK(view_contains(&ui, "Cannot save"));
    exchange(&ui, "SETTINGS demo live", TEST_SETTINGS);
    CHECK(!strcmp(view(&ui).items[1], "Start with the car: off"));
    /* App shortcut: each press moves the application to the next iDrive entry,
     * then to none (shortcut_ui.hpp); the page says where iDrive shows it. */
    press(&ui, nbt::gui::KeyDown, 1);
    CHECK(ui.focus == ui.shortcutItem());
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SET manager shortcut1 demo", "OK");
    exchange(
        &ui, "SETTINGS demo live",
        "SETTINGS\t1\trestart+shortcut1\tbackground|Background color|blue|blue,red,green,black");
    CHECK(ui.focus == ui.shortcutItem() && !strcmp(view(&ui).items[2], "App shortcut: 1"));
    CHECK(view_contains(&ui, "In iDrive: Media > App shortcut 1, storable on a favourite button"));
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SET manager shortcut2 demo", "OK");
    exchange(
        &ui, "SETTINGS demo live",
        "SETTINGS\t1\trestart+shortcut3\tbackground|Background color|blue|blue,red,green,black");
    CHECK(!strcmp(view(&ui).items[2], "App shortcut: 3"));
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "SET manager shortcut3 off", "OK");
    exchange(&ui, "SETTINGS demo live", TEST_SETTINGS);
    CHECK(!strcmp(view(&ui).items[2], "App shortcut: off") && !view_contains(&ui, "In iDrive"));
    /* Stop keeps working below them. */
    press(&ui, nbt::gui::KeyDown, 1);
    CHECK(ui.focus == ui.stopItem());
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "STOP demo", "OK");
    /* The manager's own page has no such row; an unknown flag is a protocol error. */
    UiController manager;
    loaded(&manager, "APPS\t1\tdemo", "RUNNING\t0");
    manager_settings(&manager);
    CHECK(!view_contains(&manager, "Start with the car"));
    UiController bad;
    loaded(&bad, "APPS\t1\tdemo", "RUNNING\t0");
    press(&bad, nbt::gui::KeyOption, 1);
    {
        Request request;
        CHECK(bad.nextRequest(now, request) == 1 && !strcmp(request.command, "SETTINGS demo live"));
        CHECK(bad.reply("SETTINGS\t0\trestart+autostart+shortcut4", now) == -1);
        CHECK(bad.screen == nbt::gui::Error && bad.fatal);
    }
}

static void test_files_screens(void) {
    UiController ui;
    UiView v;
    loaded(&ui, "APPS\t1\tsnes", "RUNNING\t0");
    options(&ui, "SETTINGS snes live", "SETTINGS\t0\trestart+files");
    v = view(&ui);
    CHECK(v.itemCount == 4 && !strcmp(v.items[0], "Start with the car: off") &&
          !strcmp(v.items[1], "App shortcut: off") && !strcmp(v.items[2], "Files") &&
          !strcmp(v.items[3], "Uninstall snes"));
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "FILES snes first", FILES_PAGE_ONE);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::Files && !strcmp(v.title, "snes: files"));
    CHECK(view_contains(&ui, "13 files, 3.0 MB of 1.0 GB used"));
    CHECK(v.itemCount == 14 && !strcmp(v.items[0], "Import from a USB stick") &&
          !strcmp(v.items[1], "A.sfc  (1.0 MB)") && !strcmp(v.items[2], "B.sfc  (1 KB)") &&
          !strcmp(v.items[13], "More files"));
    /* The next page, then back to the first. */
    while (ui.focus < 13) {
        press(&ui, nbt::gui::KeyDown, 1);
    }
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "FILES snes next", "FILES\t13\t3145728\t1073741824\t5000000000\t0\tM.sfc\t2048");
    v = view(&ui);
    CHECK(v.itemCount == 2 && !strcmp(v.items[1], "M.sfc  (2 KB)") && ui.focus == 0);
    press(&ui, nbt::gui::KeyBack, 1);
    exchange(&ui, "FILES snes first", FILES_PAGE_ONE);
    /* Deleting asks first, No focused. */
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    v = view(&ui);
    CHECK(ui.screen == nbt::gui::FileDelete && !strcmp(v.title, "Delete A.sfc?") && v.focus == 0);
    CHECK(!strcmp(v.items[0], "No") && !strcmp(v.items[1], "Yes, delete"));
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::Files && ui.focus == 1 && !ui.queued);
    press(&ui, nbt::gui::KeySelect, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "FILES DELETE snes 0", "OK");
    CHECK(ui.screen == nbt::gui::Files && view_contains(&ui, "A.sfc deleted"));
    exchange(&ui, "FILES snes first", "FILES\t0\t0\t1073741824\t5000000000\t0");
    CHECK(view_contains(&ui, "No files yet") && view(&ui).itemCount == 1);
    /* From a USB stick: a scan polled once a second, then the files found. */
    press(&ui, nbt::gui::KeySelect, 1);
    CHECK(ui.screen == nbt::gui::UsbFiles && view(&ui).busy);
    exchange(&ui, "FILES SCAN snes", "OK");
    CHECK(view_contains(&ui, "Looking for files on USB sticks") && view(&ui).itemCount == 0);
    exchange(&ui, "FILES USB snes 0", "USB\tscanning\t-\t0\t0\t0\t0");
    idle(&ui); // the next poll waits a second
    now += nbt::gui::kPollMs;
    exchange(&ui, "FILES USB snes 0",
             "USB\tscanned\t-\t0\t0\t0\t2\tB.sfc\t512\t0\tC.smc\t2097152\t1");
    v = view(&ui);
    CHECK(view_contains(&ui, "2 found") && v.itemCount == 3 && !v.busy);
    CHECK(!strcmp(v.items[0], "Import all (2)") && !strcmp(v.items[1], "B.sfc  (1 KB)") &&
          !strcmp(v.items[2], "C.smc  (on the car)"));
    now += nbt::gui::kPollMs;
    idle(&ui); // scanned: no more polls
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeyDown, 1);
    press(&ui, nbt::gui::KeySelect, 1); // already on the car: nothing to do
    CHECK(!ui.queued);
    press(&ui, nbt::gui::KeyUp, 1);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "FILES IMPORT snes 0", "OK");
    exchange(&ui, "FILES USB snes 0",
             "USB\timporting\t-\t0\t1\t40\t2\tB.sfc\t512\t0\tC.smc\t2097152\t1");
    v = view(&ui);
    CHECK(view_contains(&ui, "Importing 1 of 1") && v.progress == 40 && v.busy && ui.updating());
    now += nbt::gui::kPollMs;
    exchange(&ui, "FILES USB snes 0",
             "USB\tfailed\tquota\t0\t1\t0\t3\tB.sfc\t512\t0\tC.smc\t2097152\t1\tTekken 3/\t512\t0");
    CHECK(view_contains(&ui, "Import stopped: the space for this application is full"));
    CHECK(ui.screen == nbt::gui::UsbFiles && view(&ui).itemCount == 4); /* a game folder lists */
    CHECK(!ui.updating());
    /* Back on the files, refreshed; a refused scan says why. */
    press(&ui, nbt::gui::KeyBack, 1);
    exchange(&ui, "FILES snes first", "FILES\t1\t512\t1073741824\t5000000000\t0\tB.sfc\t512");
    CHECK(ui.screen == nbt::gui::Files && view(&ui).itemCount == 2);
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "FILES SCAN snes", "ERR busy");
    CHECK(view_contains(&ui, "Import stopped: Busy; try again in a moment"));
    press(&ui, nbt::gui::KeyBack, 1);
    exchange(&ui, "FILES snes first", "FILES\t1\t512\t1073741824\t5000000000\t0\tB.sfc\t512");
    /* Game folders ("<folder>/") and UTF-8 names list as they are. */
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == ui.filesItem());
    press(&ui, nbt::gui::KeySelect, 1);
    exchange(&ui, "FILES snes first",
             "FILES\t2\t1024\t1073741824\t5000000000\t0\tPok\xc3\xa9mon.sfc\t512\tTekken 3/\t512");
    CHECK(ui.screen == nbt::gui::Files && view(&ui).itemCount == 3);
    /* Back to Settings, on the Files entry; a garbled listing is a protocol error. */
    press(&ui, nbt::gui::KeyBack, 1);
    CHECK(ui.screen == nbt::gui::Settings && ui.focus == ui.filesItem() && !ui.queued);
    press(&ui, nbt::gui::KeySelect, 1);
    {
        Request request;
        CHECK(ui.nextRequest(now, request) == 1 && !strcmp(request.command, "FILES snes first"));
        CHECK(ui.reply("FILES\t1\t512\t1073741824\t5000000000\t0\t../x.sfc\t1", now) == -1);
        CHECK(ui.screen == nbt::gui::Error && ui.fatal);
    }
    /* An application without files has no entry; sizes are written in the car's language. */
    loaded(&ui, "APPS\t1\tdemo", "RUNNING\t0");
    options(&ui, "SETTINGS demo live", "SETTINGS\t0\trestart");
    CHECK(view(&ui).itemCount == 3 &&
          !view_contains(&ui, "Files")); /* Start with the car, App shortcut, Uninstall */
    char size[32];
    nbt::gui::setCurrentLanguage(nbt::model::LangFr);
    nbt::gui::formatSize(UINT64_C(3) << 20, size, sizeof(size));
    CHECK(!strcmp(size, "3,0 Mo"));
    nbt::gui::setCurrentLanguage(nbt::model::LangEn);
    nbt::gui::formatSize(UINT64_C(5) << 30, size, sizeof(size));
    CHECK(!strcmp(size, "5.0 GB"));
}

int main(void) {
    test_files_screens();
    test_autostart_setting();
    test_application_status();
    test_main_menu_and_banner();
    test_push_starts();
    test_application_needing_an_update();
    test_service_rows();
    test_hold_stops_and_option_opens_settings();
    test_stop_failures();
    test_uninstall();
    test_new_application();
    test_application_on_the_panel_ends();
    test_install_flow();
    test_recovery_and_reject();
    test_package_change_requires_new_review();
    test_pairing_exits();
    test_pairing_timeout();
    test_setup_and_idrive_restart();
    test_tools_menu();
    test_self_test();
    test_manager_update_review();
    test_pairing_wifi();
    test_malformed_replies();
    test_errors_and_misuse();
    test_app_settings();
    test_status_page();
    test_rendering();
    test_pairing_qr();
    test_quarantine_unblock_and_abort_app();
    return 0;
}
