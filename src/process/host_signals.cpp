// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/host_signals.hpp"

#include <signal.h>
#include <string.h>

namespace nbt {
namespace process {
namespace {

volatile sig_atomic_t stopping;
volatile sig_atomic_t showRequested;
volatile sig_atomic_t pauseRequested;

void onStop(int) {
    stopping = 1;
}

void onShow(int) {
    showRequested = 1;
}

void onPause(int) {
    pauseRequested = 1;
}

} // namespace

int installHostSignals() {
    struct sigaction action;
    stopping = showRequested = pauseRequested = 0;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = onStop;
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) {
        return -1;
    }
    action.sa_handler = onShow;
    if (sigaction(SIGUSR1, &action, NULL)) {
        return -1;
    }
    action.sa_handler = onPause;
    if (sigaction(SIGUSR2, &action, NULL)) {
        return -1;
    }
    action.sa_handler = SIG_IGN;
    return (sigaction(SIGHUP, &action, NULL) || sigaction(SIGPIPE, &action, NULL)) ? -1 : 0;
}

bool hostStopping() {
    return stopping != 0;
}

void stopHost() {
    stopping = 1;
}

bool showPending() {
    return showRequested != 0;
}

void showDone() {
    showRequested = 0;
}

bool takePause() {
    const bool asked = pauseRequested != 0;
    pauseRequested = 0;
    return asked;
}

} // namespace process
} // namespace nbt
