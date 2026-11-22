// SPDX-License-Identifier: MPL-2.0
// The application the host tests start (build/test-app-host): the bridge
// handled by hand on the manager's own code, its window on tests/fake_screen.
// The kits' test application, built for the car, is apps/test.
#include "nbt/bridge.h"
#include "nbt/controller_config.h"
#include "nbt/fs.h"
#include "nbt/gui/frame.hpp"
#include "nbt/gui/screen_window.hpp"
#include "nbt/most_input.h"
#include "nbt/settings.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#ifndef NBT_TEST_VERSION
#define NBT_TEST_VERSION "v1"
#endif
#define MANAGER_ROOT "/var/opt/nbt-manager"

static volatile sig_atomic_t stopping;
static volatile sig_atomic_t show_requested;
static void on_signal(int signalNumber) {
    if (signalNumber == SIGUSR1) {
        show_requested = 1;
    } else {
        stopping = 1;
    }
}

static int parse_int(const char *text, long min, int *out) {
    char *end = NULL;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (errno || !end || end == text || *end || value < min || value > INT_MAX) {
        return -1;
    }
    *out = static_cast<int>(value);
    return 0;
}

static uint64_t now_ms(void) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time)) {
        return 0;
    }
    return static_cast<uint64_t>(time.tv_sec) * 1000u +
           static_cast<uint64_t>(time.tv_nsec) / 1000000u;
}

static int start_input(void) {
    const char *root = MANAGER_ROOT, *library = NULL;
    nbt_ccs_config config;
    nbt_fs fs;
    char error[128];
#ifdef NBT_TESTING
    if (getenv("NBT_TEST_ROOT")) {
        root = getenv("NBT_TEST_ROOT");
    }
    library = getenv("NBT_TEST_INPUT_LIBRARY");
#endif
    if (nbt_fs_init(&fs, root, error, sizeof(error))) {
        nbt_ccs_defaults(&config);
    } else {
        (void)nbt_controller_config_load(&fs, &config);
    }
    int fd = nbt_most_input_start(library, &config);
    if (fd >= FD_SETSIZE) {
        close(fd);
        fd = -1;
    }
    return fd;
}

int main(int argc, char **argv) {
    nbt::gui::ScreenWindow screen;
    int readyFd = -1, leaseFd = -1, statusFd = -1, inputFd = -1;
    nbt_settings settings;
    const char *settingsFile = NULL;
    int visible = 1, result = 1;
    const char *launch = getenv("NBT_LAUNCH");
    const char *caps = getenv("NBT_BRIDGE_CAPS");
    struct sigaction action;
    if (argc == 5 && !strcmp(argv[1], "--ready-fd") && !strcmp(argv[3], "--lease-fd")) {
        struct flock lease;
        if (parse_int(argv[2], 3, &readyFd) || parse_int(argv[4], 3, &leaseFd) ||
            readyFd == leaseFd) {
            return 2;
        }
        memset(&lease, 0, sizeof(lease));
        lease.l_type = F_RDLCK;
        lease.l_whence = SEEK_SET;
        if (fcntl(leaseFd, F_SETLK, &lease) < 0) {
            return 1;
        }
    } else if (argc != 1) {
        fprintf(stderr, "usage: nbt-test [--ready-fd FD --lease-fd FD]\n");
        return 2;
    }
    const int user = readyFd >= 0 && launch && !strcmp(launch, "user");
    // The manager GUI puts a window bound to this LayerManager surface on the
    // panel and takes it off again (MENU); without one the car never draws it.
    // Such a window cannot step aside by itself, so it does not hide.
    int surface = 0;
    if (user && getenv("NBT_SURFACE") && parse_int(getenv("NBT_SURFACE"), 1, &surface)) {
        return 1;
    }
    if (user && nbt_bridge_caps_valid(caps) && nbt_bridge_cap_has(caps, "status") &&
        getenv("NBT_STATUS_FD") && !strcmp(getenv("NBT_STATUS_FD"), "5")) {
        statusFd = 5;
    }
    memset(&settings, 0, sizeof(settings));
    if (nbt_settings_add(&settings, "background|Background color|blue|blue,red,green,black") ||
        nbt_settings_add(&settings, "controller|Use the controller|on|on,off")) {
        return 1;
    }
    if (getenv("NBT_SETTING_BACKGROUND") &&
        nbt_setting_allows(&settings.items[0], getenv("NBT_SETTING_BACKGROUND"))) {
        strcpy(settings.items[0].value, getenv("NBT_SETTING_BACKGROUND"));
    }
    if (getenv("NBT_SETTING_CONTROLLER") &&
        nbt_setting_allows(&settings.items[1], getenv("NBT_SETTING_CONTROLLER"))) {
        strcpy(settings.items[1].value, getenv("NBT_SETTING_CONTROLLER"));
    }
    if (nbt_bridge_caps_valid(caps) && nbt_bridge_cap_has(caps, "live-settings")) {
        settingsFile = getenv("NBT_SETTINGS_FILE");
        if (settingsFile) {
            (void)nbt_settings_values_read(settingsFile, &settings);
        }
    }
    const int canShow = user && nbt_bridge_caps_valid(caps) && nbt_bridge_cap_has(caps, "show");
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL) ||
        sigaction(SIGUSR1, &action, NULL)) {
        return 1;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL)) {
        return 1;
    }

    if (surface && !screen.open(640, 240, surface).ok()) {
        fprintf(stderr, "no window on surface %d: the car will not draw it\n", surface);
        surface = 0; // still runs; the manager reports that it did not appear
    }
    if (!screen.opened() && !screen.open(640, 240).ok()) {
        fprintf(stderr, "could not create test window\n");
        goto done;
    }
    if (user && strcmp(settings.items[1].value, "off")) {
        inputFd = start_input();
    }
    if (!screen.frame()
             .hello(NBT_TEST_VERSION, settings.items[0].value, inputFd >= 0 ? "BACK TO EXIT" : NULL)
             .ok() ||
        !screen.post().ok() || !screen.setVisible(true).ok()) {
        fprintf(stderr, "could not display test message\n");
        goto done;
    }
    {
        uint64_t started = now_ms(), heartbeat = started, shownMs = started;
        while (!stopping) {
            const uint64_t now = now_ms();
            if (now - heartbeat >= 1000u || now == started) {
#ifdef NBT_TEST_FAIL_AFTER
                if (now - started >= NBT_TEST_FAIL_AFTER * 1000u) {
                    goto done;
                }
#endif
                if (settingsFile) {
                    nbt_settings latest = settings;
                    if (!nbt_settings_values_read(settingsFile, &latest) &&
                        strcmp(latest.items[0].value, settings.items[0].value)) {
                        settings = latest;
                        if (!screen.frame()
                                 .hello(NBT_TEST_VERSION, settings.items[0].value,
                                        inputFd >= 0 ? "BACK TO EXIT" : NULL)
                                 .ok() ||
                            (visible && !screen.post().ok())) {
                            goto done;
                        }
                    }
                }
                if (statusFd >= 0) {
                    char message[NBT_BRIDGE_STATUS_MAX + 1], line[NBT_BRIDGE_STATUS_LINE_MAX + 1];
                    snprintf(message, sizeof(message), "%s %s", visible ? "Hello" : "Hidden",
                             NBT_TEST_VERSION);
                    const int length = nbt_bridge_status_format(now, message, line, sizeof(line));
                    if (length > 0) {
                        (void)nbt_fs_status_write(statusFd, line, static_cast<size_t>(length));
                    }
                }
                if (readyFd >= 0 && write(readyFd, "R", 1) != 1) {
                    break;
                }
                if (now != started) {
                    heartbeat = now;
                }
            }
            if (show_requested) {
                show_requested = 0;
                if (canShow) {
                    visible = 1;
                    if (!screen.post().ok() || !screen.setVisible(true).ok()) {
                        goto done;
                    }
                    shownMs = now;
                }
            }
            if (inputFd < 0 && visible && !surface && now - shownMs >= 5000u) {
                visible = 0;
                if (!screen.setVisible(false).ok()) {
                    goto done;
                }
                if (readyFd < 0) {
                    break;
                }
            }
            {
                fd_set readers;
                struct timeval wait = {0, 100000};
                FD_ZERO(&readers);
                if (inputFd >= 0) {
                    FD_SET(inputFd, &readers);
                }
                if (select(inputFd >= 0 ? inputFd + 1 : 0, &readers, NULL, NULL, &wait) < 0 &&
                    errno != EINTR) {
                    goto done;
                }
            }
            if (inputFd >= 0) {
                unsigned char byte;
                ssize_t n;
                while ((n = read(inputFd, &byte, 1)) == 1) {
                    if (byte == NBT_INPUT_BACK || byte == NBT_INPUT_HOME) {
                        stopping = 1;
                    }
                }
                if (n == 0) {
                    close(inputFd);
                    inputFd = -1;
                }
            }
        }
    }
    result = 0;
done:
    if (inputFd >= 0) {
        (void)nbt_most_input_stop(300);
    }
    if (readyFd >= 0) {
        close(readyFd);
    }
    if (leaseFd >= 0) {
        close(leaseFd);
    }
    if (statusFd >= 0) {
        close(statusFd);
    }
    screen.close();
    if (inputFd >= 0) {
        close(inputFd);
    }
    return result;
}
