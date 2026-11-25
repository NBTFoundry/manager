// SPDX-License-Identifier: MPL-2.0
/* Desktop preview of the manager GUI (X11). It runs the real GUI controller
 * and renderer against a simulated worker with canned replies, to check the
 * look and navigation. It installs nothing and opens no network session.
 *
 * Keys: Up/Down or mouse wheel to move, Enter/Space or click to select (hold
 * on an application for its settings), Backspace/Escape/Left to go back, M
 * for the iDrive MENU key, Q or Ctrl+C to quit. On the pairing screen, U
 * simulates the phone: the first press opens the upload page, the second
 * uploads a package for "test". Starting an application closes the manager.
 * "test" then shows the test application's own screen (its background colour
 * from Settings) until Backspace/Escape (BACK) or M; the simulator then opens
 * the manager again, as it does at once for "media-player".
 * Languages: NBT_LANGUAGE is the Language setting (auto|en|fr|de|it|es|zh|ja,
 * default auto); NBT_SIM_CAR_LANGUAGE is iDrive's vehicle.language number
 * (nbt/model/language.hpp; 1 German, 6 French, 11 Chinese, 13 Japanese...),
 * unset as when the plugin reported none. Settings > Language changes it.
 * Usage: nbt-simulator [scale] */
#include "nbt/core/system.hpp"
#include "nbt/gui/frame.hpp"
#include "nbt/gui/qr_code.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/ui_renderer.hpp"
#include "nbt/model/language.hpp"
#include "nbt/settings.h"
#include "nbt/util.h"

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

enum { W = 640, H = 240 };

static volatile sig_atomic_t interrupted;
static void on_signal(int number) {
    (void)number;
    interrupted = 1;
}

/* Simulated worker: U opens the page and then uploads a package; an approved
 * installation completes after a short health check. */
/* Two applications; the first is running, to try stopping it from its settings. */
#define APPS 2
static const char *const apps[APPS] = {"media-player", "test"};
static struct {
    int state; /* STATUS state number, see PROTOCOL.md */
    char app[48];
    unsigned installed; /* installed version number */
    uint64_t since_ms;
    char digest[65];
    int running[APPS];
    int stopping; /* index being stopped, or -1 */
    uint64_t stop_ms;
    int started; /* 1 + the application a START started: the manager closes */
    char message[64];
    int autostart[APPS]; /* Start with the car: 0 off, 1 on, 2 on but paused (media-player) */
} w = {0, "", 1, 0, "", {1, 0}, -1, 0, 0, "-", {2, 0}};

/* What each application declares in its package. */
static const char *const declared[APPS] = {NULL,
                                           "background|Background color|blue|blue,red,green,black"};
static nbt_settings settings[APPS];
/* The manager's own (src/lifecycle/manager_settings.cpp), kept in memory here. */
static const char *const manager_declared[] = {
    "debug_log|Debug log|off|off,on", "invert|Invert knob|off|off,on",
    "release|Hand back controller|off|off,on",
    "native_audio|Native audio|interrupt|interrupt,media"};
static nbt_settings manager_settings;
static bool pairing = false, phonePending = false;

static void declare(void) {
    size_t i;
    for (i = 0; i < APPS; ++i) {
        if (declared[i] && nbt_settings_add(&settings[i], declared[i])) {
            abort();
        }
    }
    for (i = 0; i < sizeof(manager_declared) / sizeof(manager_declared[0]); ++i) {
        if (nbt_settings_add(&manager_settings, manager_declared[i])) {
            abort();
        }
    }
}

static int app_index(const char *name) {
    int i;
    for (i = 0; i < APPS; ++i) {
        if (!strcmp(apps[i], name)) {
            return i;
        }
    }
    return -1;
}

static nbt_settings *settings_of(const char *name) {
    if (!strcmp(name, "manager")) {
        return &manager_settings;
    }
    return app_index(name) >= 0 ? &settings[app_index(name)] : NULL;
}

/* The language setting and iDrive's language, from the environment. */
static void sim_language(nbt::gui::UiController &ui) {
    const char *setting = getenv("NBT_LANGUAGE"), *car = getenv("NBT_SIM_CAR_LANGUAGE");
    nbt::model::Language language = nbt::model::LangEn;
    if (setting && !nbt::model::languageSettingParse(setting, ui.languageChoice)) {
        fprintf(stderr, "NBT_LANGUAGE is one of auto, en, fr, de, it, es, zh, ja\n");
    }
    ui.carLanguage = car && nbt::model::languageFromCar(static_cast<unsigned>(atoi(car)), language)
                         ? language
                         : -1;
    ui.applyLanguage();
}

/* The banner's version: NBT_SIM_VERSION, or "sim". */
static const char *sim_version(void) {
    const char *value = getenv("NBT_SIM_VERSION");
    return value && *value ? value : "sim";
}

/* A fixed review digest (NBT_SIM_DIGEST, 64 hex digits) for repeatable captures. */
static bool fixed_digest(char *out) {
    const char *value = getenv("NBT_SIM_DIGEST");
    if (!value || strlen(value) != 64 || strspn(value, "0123456789abcdef") != 64) {
        return false;
    }
    memcpy(out, value, 65);
    return true;
}

static void random_hex(char *out, size_t bytes) {
    unsigned char raw[32];
    size_t i;
    if (bytes > sizeof(raw) || nbt_random(raw, bytes)) {
        memset(raw, 0x5a, sizeof(raw));
    }
    for (i = 0; i < bytes; ++i) {
        sprintf(out + 2 * i, "%02x", raw[i]);
    }
}

/* Settings > Files for media-player (capability `files`): its files, and a USB
 * stick whose scan takes a second and whose import takes two. */
static struct {
    char names[24][40];
    unsigned count;
    int phase; /* 0 idle, 1 scanning, 2 scanned, 3 importing, 4 imported */
    uint64_t since;
    int which; /* -1 all */
} sim_files = {{"Morning drive.mp3", "Podcast 12.mp3"}, 2, 0, 0, -1};
static const char *const sim_stick[] = {"Album 1 - Track 01.mp3", "Album 1 - Track 02.mp3",
                                        "Evening.flac"};

static bool sim_stored(const char *name) {
    for (unsigned i = 0; i < sim_files.count; ++i) {
        if (!strcmp(sim_files.names[i], name)) {
            return true;
        }
    }
    return false;
}

static void sim_files_command(const char *command, char *reply, size_t size, uint64_t now) {
    char verb[16] = "", app[49] = "", arg[16] = "";
    size_t used;
    if (sscanf(command + 6, "%15s %48s %15s", verb, app, arg) < 2) {
        snprintf(reply, size, "ERR invalid-command");
        return;
    }
    if (strcmp(verb, "DELETE") && strcmp(verb, "SCAN") && strcmp(verb, "USB") &&
        strcmp(verb, "IMPORT")) { // FILES <app> first|next
        used = (size_t)snprintf(reply, size, "FILES\t%u\t%u\t1073741824\t9000000000\t0",
                                sim_files.count, sim_files.count * 4500000u);
        for (unsigned i = 0; i < sim_files.count && used < size; ++i) {
            used +=
                (size_t)snprintf(reply + used, size - used, "\t%s\t4500000", sim_files.names[i]);
        }
        return;
    }
    if (!strcmp(verb, "DELETE")) {
        const unsigned i = (unsigned)atoi(arg);
        if (i < sim_files.count) {
            memmove(sim_files.names[i], sim_files.names[i + 1],
                    (sim_files.count - i - 1) * sizeof(sim_files.names[0]));
            --sim_files.count;
        }
        snprintf(reply, size, "OK");
        return;
    }
    if (!strcmp(verb, "SCAN") || !strcmp(verb, "IMPORT")) {
        sim_files.phase = verb[0] == 'S' ? 1 : 3;
        sim_files.since = now;
        sim_files.which = strcmp(arg, "all") ? atoi(arg) : -1;
        snprintf(reply, size, "OK");
        return;
    }
    if ((sim_files.phase == 1 || sim_files.phase == 3) && now - sim_files.since > 2000) {
        for (int i = 0; sim_files.phase == 3 && i < 3; ++i) {
            if ((sim_files.which < 0 || sim_files.which == i) && !sim_stored(sim_stick[i]) &&
                sim_files.count < 24) {
                snprintf(sim_files.names[sim_files.count++], 40, "%s", sim_stick[i]);
            }
        }
        ++sim_files.phase;
    }
    static const char *const phases[] = {"idle", "scanning", "scanned", "importing", "imported"};
    const uint64_t elapsed = now - sim_files.since;
    used = (size_t)snprintf(
        reply, size, "USB\t%s\t-\t%d\t%d\t%u\t%d", phases[sim_files.phase],
        sim_files.phase == 3 ? (int)(elapsed / 700) : 0, sim_files.which < 0 ? 3 : 1,
        sim_files.phase == 3 ? (unsigned)(elapsed / 20) : 0, sim_files.phase == 1 ? 0 : 3);
    for (int i = 0; sim_files.phase != 1 && i < 3 && used < size; ++i) {
        used +=
            (size_t)snprintf(reply + used, size - used, "\t%s\t%u\t%d", sim_stick[i],
                             3200000u + 900000u * (unsigned)i, sim_stored(sim_stick[i]) ? 1 : 0);
    }
}

static void worker(const char *command, char *reply, size_t size, uint64_t now) {
    if (!strncmp(command, "FILES ", 6)) {
        sim_files_command(command, reply, size, now);
        return;
    }
    if (w.stopping >= 0 && now - w.stop_ms > 1500) {
        w.running[w.stopping] = 0;
        w.stopping = -1;
        snprintf(w.message, sizeof(w.message), "Application stopped");
    }
    if (!strcmp(command, "LIST")) {
        snprintf(reply, size, "APPS\t%d\t%s\t%s", APPS, apps[0], apps[1]);
    } else if (!strcmp(command, "SERVICES")) {
        snprintf(reply, size, "SERVICES\t0");
    } else if (!strcmp(command, "RUNNING status")) {
        int i, count = 0;
        size_t used;
        for (i = 0; i < APPS; ++i) {
            count += w.running[i];
        }
        used = (size_t)snprintf(reply, size, "RUNNING\t%d", count);
        for (i = 0; i < APPS && used < size; ++i) {
            if (w.running[i]) {
                used += (size_t)snprintf(reply + used, size - used, "\t%s%s", apps[i],
                                         i == 1 ? "|Hello v1" : "");
            }
        }
    } else if (!strncmp(command, "SESSION ", 8)) {
        w.app[0] = 0;
        w.state = 1;
        w.since_ms = now;
        snprintf(reply, size, "SESSION\t172.16.222.1:8080\tDIRECT-03-DEMO\tdevelopment-only");
    } else if (!strcmp(command, "PHONE OPEN")) {
        pairing = true;
        snprintf(reply, size, "OK");
    } else if (!strcmp(command, "PHONE STATUS")) {
        snprintf(reply, size, "%s",
                 phonePending ? "PHONE\tpair\t0123456789abcdef0123456789abcdef\tDemo phone\t123456"
                              : "PHONE\tnone");
    } else if (!strncmp(command, "PHONE YES ", 10) || !strncmp(command, "PHONE NO ", 9)) {
        phonePending = pairing = false;
        snprintf(reply, size, command[6] == 'Y' ? "OK paired" : "OK cancelled");
    } else if (!strcmp(command, "PHONE LIST")) {
        snprintf(reply, size, "PHONES\t0");
    } else if (!strcmp(command, "STATUS")) {
        if (w.state == 3 && now - w.since_ms > 4000) {
            w.state = 4;
            ++w.installed;
        }
        switch (w.state) {
        case 1:
        case 6:
            snprintf(reply, size, "STATUS\t%d\t-\t-\t-\t0\t-\tReady for upload", w.state);
            break;
        case 2:
        case 3:
            snprintf(reply, size, "STATUS\t%d\t%s\tv%u\tv%u\t%u\t%s\t%s", w.state, w.app,
                     w.installed, w.installed + 1, w.installed + 1, w.digest,
                     w.state == 2 ? "Package verified; approve in iDrive"
                                  : "Checking application health");
            break;
        case 4:
            snprintf(reply, size, "STATUS\t4\t%s\tv%u\t-\t0\t-\tUpdate installed", w.app,
                     w.installed);
            break;
        default:
            snprintf(reply, size, "STATUS\t0\t-\t-\t-\t0\t-\t%s", w.message);
            break;
        }
    } else if (!strncmp(command, "APPROVE ", 8)) {
        w.state = 3;
        w.since_ms = now;
        snprintf(reply, size, "OK installing");
    } else if (!strcmp(command, "CANCEL")) {
        w.state = 0;
        pairing = phonePending = false;
        snprintf(reply, size, "OK");
    } else if (!strncmp(command, "SETTINGS ", 9)) {
        char app[49];
        const nbt_settings *list;
        size_t used, i;
        if (sscanf(command + 9, "%48s", app) != 1 || !(list = settings_of(app))) {
            snprintf(reply, size, "ERR settings");
            return;
        }
        const int index = app_index(app);
        const int autostart = index >= 0 ? w.autostart[index] : 0;
        used = (size_t)snprintf(reply, size, "SETTINGS\t%u\t%s%s", (unsigned)list->count,
                                !strcmp(app, "test")           ? "live"
                                : !strcmp(app, "media-player") ? "restart+files"
                                                               : "restart",
                                autostart == 2   ? "+autostart-paused"
                                : autostart == 1 ? "+autostart"
                                                 : "");
        for (i = 0; i < list->count && used + 1 < size; ++i) {
            int n;
            reply[used++] = '\t';
            n = nbt_setting_format(&list->items[i], reply + used, size - used);
            if (n < 0) {
                break;
            }
            used += (size_t)n;
        }
    } else if (!strncmp(command, "AUTOSTART ", 10)) {
        char app[49], mode[4];
        const int index = sscanf(command + 10, "%48s %3s", app, mode) == 2 ? app_index(app) : -1;
        if (index < 0 || (strcmp(mode, "on") && strcmp(mode, "off"))) {
            snprintf(reply, size, "ERR invalid-command");
            return;
        }
        /* Switching does not clear the pause: only a start by hand does. */
        w.autostart[index] = !strcmp(mode, "off") ? 0 : w.autostart[index] == 2 ? 2 : 1;
        snprintf(reply, size, "OK");
    } else if (!strncmp(command, "SET ", 4)) {
        char app[49], key[17], value[13];
        nbt_settings *list = NULL;
        int setting = -1, choice = 0;
        if (sscanf(command + 4, "%48s %16s %12s", app, key, value) == 3) {
            list = settings_of(app);
            if (!strcmp(app, "manager") && !strcmp(key, "language")) {
                snprintf(reply, size, "%s",
                         nbt::model::languageSettingParse(value, choice) ? "OK" : "ERR settings");
                return;
            }
        }
        if (list) {
            setting = nbt_settings_find(list, key);
        }
        if (setting >= 0 && nbt_setting_allows(&list->items[setting], value)) {
            strcpy(list->items[setting].value, value);
            snprintf(reply, size, "OK");
        } else {
            snprintf(reply, size, "ERR settings");
        }
    } else if (!strncmp(command, "START ", 6) && app_index(command + 6) >= 0) {
        int index = app_index(command + 6);
        if (w.autostart[index] == 2) {
            w.autostart[index] = 1; /* a start by hand resumes automatic starts */
        }
        if (w.running[index] && index == 0) {
            snprintf(reply, size, "ERR running");
        } else {
            w.running[index] = 1;
            w.started = 1 + index;
            snprintf(reply, size, "OK");
        }
    } else if (!strncmp(command, "STOP ", 5) && app_index(command + 5) >= 0) {
        w.stopping = app_index(command + 5);
        w.stop_ms = now;
        snprintf(w.message, sizeof(w.message), "Stopping application");
        snprintf(reply, size, "OK");
    } else {
        snprintf(reply, size, "ERR invalid-command");
    }
}

static void phone(void) {
    if (pairing) {
        phonePending = true;
    } else if (w.state == 1 || w.state == 0) {
        w.state = 2;
        if (!fixed_digest(w.digest)) {
            random_hex(w.digest, 32);
        }
        snprintf(w.app, sizeof(w.app), "test");
    }
}

static int render(const nbt::gui::UiController &ui, nbt::gui::Frame &frame) {
    nbt::gui::UiView view;
    nbt::gui::QrCode qr;
    char link[160] = {0};
    if (!ui.view(view).ok()) {
        return -1;
    }
    const bool withQr =
        view.screen == nbt::gui::Pairing &&
        ui.pairingLink(nbt::core::Span<char>(link, sizeof(link))).ok() &&
        qr.encode(reinterpret_cast<const unsigned char *>(link), strlen(link), -1).ok();
    nbt::gui::UiRenderer renderer;
    return renderer.render(view, withQr ? &qr : NULL, frame).ok() ? 0 : -1;
}

/* Nearest-neighbour upscale; the frame's 0xAARRGGBB words match a 24-bit X visual. */
static void blit(const unsigned char *pixels, XImage *image, int scale) {
    int x, y;
    for (y = 0; y < H * scale; ++y) {
        for (x = 0; x < W * scale; ++x) {
            uint32_t value;
            memcpy(&value, pixels + ((size_t)(y / scale) * W + (size_t)(x / scale)) * 4,
                   sizeof(value));
            XPutPixel(image, x, y, value & 0xffffffu);
        }
    }
}

int main(int argc, char **argv) {
    static unsigned char pixels[W * H * 4];
    nbt::gui::Frame frame = {pixels, sizeof(pixels), W, H, W * 4};
    int scale = argc > 1 ? atoi(argv[1]) : 2, screen, dirty = 1, running = 1, select_down = 0;
    int showing = -1; /* the application on screen instead of the manager */
    Display *display;
    Window window;
    XImage *image;
    Atom close_atom;
    GC gc;
    nbt::gui::UiController ui;
    nbt::gui::UiView view = nbt::gui::UiView(), shown = nbt::gui::UiView();
    struct sigaction action;
    if (scale < 1 || scale > 4) {
        scale = 2;
    }
    /* Ctrl+C in the terminal: leave the loop and close the window cleanly. */
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    if (!(display = XOpenDisplay(NULL))) {
        fprintf(stderr, "cannot open X display\n");
        return 1;
    }
    screen = DefaultScreen(display);
    if (DefaultDepth(display, screen) < 24) {
        fprintf(stderr, "needs a 24-bit display\n");
        return 1;
    }
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, (unsigned)(W * scale),
                                 (unsigned)(H * scale), 0, 0, 0);
    XStoreName(display, window, "NBT Manager simulator");
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                     ButtonReleaseMask);
    /* Held keys repeat presses only, without synthetic releases. */
    XkbSetDetectableAutoRepeat(display, True, NULL);
    close_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &close_atom, 1);
    XMapWindow(display, window);
    gc = DefaultGC(display, screen);
    const size_t imageBytes = static_cast<size_t>(W) * static_cast<size_t>(H) * 4u *
                              static_cast<size_t>(scale) * static_cast<size_t>(scale);
    image = XCreateImage(display, DefaultVisual(display, screen),
                         (unsigned)DefaultDepth(display, screen), ZPixmap, 0,
                         static_cast<char *>(malloc(imageBytes)), (unsigned)(W * scale),
                         (unsigned)(H * scale), 32, 0);
    if (!image || !image->data || !ui.init("uap0", 8080).ok()) {
        return 1;
    }
    declare();
    ui.setVersion(sim_version());
    sim_language(ui);
    puts("Up/Down or wheel: move   Enter/click: select (hold: settings)   Backspace/Esc: back   M: "
         "MENU\n"
         "U: phone requests pairing, or uploads a package   Q/Ctrl+C: quit");
    while (running && !interrupted) {
        nbt::gui::Request request;
        char reply[nbt::gui::kReplyMax];
        fd_set readers;
        struct timeval wait = {0, 50000};
        const nbt::core::Result<uint64_t> nowResult = nbt::core::monotonicMs();
        if (!nowResult.get()) {
            break;
        }
        const uint64_t now = nowResult.value();
        while (showing < 0 && ui.nextRequest(now, request) == 1) {
            worker(request.command, reply, sizeof(reply), now);
            ui.reply(reply, now);
            dirty = 1;
        }
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == Expose) {
                dirty = 1;
            } else if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == close_atom) {
                running = 0;
            } else if (showing >= 0) {
                /* The test application: BACK or MENU ends it. */
                KeySym key = event.type == KeyPress ? XLookupKeysym(&event.xkey, 0) : NoSymbol;
                if ((event.type == KeyPress &&
                     (key == XK_BackSpace || key == XK_Escape || key == XK_Left ||
                      (key == XK_m && !(event.xkey.state & ControlMask)))) ||
                    (event.type == ButtonPress && event.xbutton.button == Button3)) {
                    printf("%s exited; opening the manager again.\n", apps[showing]);
                    w.running[showing] = 0;
                    showing = -1;
                } else if (key == XK_q || (key == XK_c && (event.xkey.state & ControlMask))) {
                    running = 0;
                }
                dirty = 1;
            } else if (event.type == ButtonPress) {
                if (event.xbutton.button == Button4) {
                    ui.press(nbt::gui::KeyUp);
                } else if (event.xbutton.button == Button5) {
                    ui.press(nbt::gui::KeyDown);
                } else if (event.xbutton.button == Button1) {
                    ui.press(nbt::gui::KeySelect);
                } else if (event.xbutton.button == Button3) {
                    ui.press(nbt::gui::KeyBack);
                }
                dirty = 1;
            } else if (event.type == ButtonRelease) {
                if (event.xbutton.button == Button1) {
                    ui.release(nbt::gui::KeySelect);
                }
                dirty = 1;
            } else if (event.type == KeyRelease) {
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if ((key == XK_Return || key == XK_KP_Enter || key == XK_space) && select_down) {
                    select_down = 0;
                    ui.release(nbt::gui::KeySelect);
                }
                dirty = 1;
            } else if (event.type == KeyPress) {
                KeySym key = XLookupKeysym(&event.xkey, 0);
                if (key == XK_Up) {
                    ui.press(nbt::gui::KeyUp);
                } else if (key == XK_Down) {
                    ui.press(nbt::gui::KeyDown);
                } else if (key == XK_Return || key == XK_KP_Enter || key == XK_space) {
                    if (!select_down) {
                        ui.press(nbt::gui::KeySelect); /* ignore auto-repeat */
                    }
                    select_down = 1;
                } else if (key == XK_BackSpace || key == XK_Escape || key == XK_Left) {
                    ui.press(nbt::gui::KeyBack);
                } else if (key == XK_m && !(event.xkey.state & ControlMask)) {
                    ui.home();
                } else if (key == XK_o) { /* the controller's OPTION */
                    ui.press(nbt::gui::KeyOption);
                } else if (key == XK_u) {
                    phone();
                } else if (key == XK_q || (key == XK_c && (event.xkey.state & ControlMask))) {
                    running = 0;
                }
                dirty = 1;
            }
        }
        if (ui.wantsExit()) {
            /* A started application takes the screen and the manager closes;
             * here the manager simply opens again. */
            if (!w.started) {
                break;
            }
            if (!strcmp(apps[w.started - 1], "test")) {
                showing = w.started - 1;
                printf("test started; Backspace/Esc (BACK) or M ends it.\n");
            } else {
                puts("Manager closed: the started application has the screen. Opening it again.");
            }
            w.started = 0;
            if (!ui.init("uap0", 8080).ok()) {
                break;
            }
            ui.setVersion(sim_version());
            dirty = 1;
            continue;
        }
        if (showing >= 0) {
            /* The test application's own drawing, with its settings. */
            char version[8];
            const nbt_settings *list = &settings[showing];
            int index = nbt_settings_find(list, "background");
            snprintf(version, sizeof(version), "v%u", w.installed);
            if (dirty) {
                if (!frame
                         .hello(version, index >= 0 ? list->items[index].value : NULL,
                                "BACK TO EXIT")
                         .ok()) {
                    break;
                }
                blit(pixels, image, scale);
                XPutImage(display, window, gc, image, 0, 0, 0, 0, (unsigned)(W * scale),
                          (unsigned)(H * scale));
                XFlush(display);
                dirty = 0;
            }
            FD_ZERO(&readers);
            FD_SET(ConnectionNumber(display), &readers);
            if (select(ConnectionNumber(display) + 1, &readers, NULL, NULL, &wait) < 0 &&
                errno != EINTR) {
                break;
            }
            continue;
        }
        if (!ui.view(view).ok()) {
            break;
        }
        if (view != shown) {
            shown = view;
            dirty = 1;
        }
        if (dirty) {
            if (render(ui, frame)) {
                fprintf(stderr, "render failed\n");
                break;
            }
            blit(pixels, image, scale);
            XPutImage(display, window, gc, image, 0, 0, 0, 0, (unsigned)(W * scale),
                      (unsigned)(H * scale));
            XFlush(display);
            dirty = 0;
        }
        FD_ZERO(&readers);
        FD_SET(ConnectionNumber(display), &readers);
        if (select(ConnectionNumber(display) + 1, &readers, NULL, NULL, &wait) < 0 &&
            errno != EINTR) {
            break;
        }
    }
    XDestroyImage(image);
    XCloseDisplay(display);
    return 0;
}
