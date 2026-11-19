#include "nbt/gui/ui_controller.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/gui/wifi_code.hpp"
#include "nbt/log.h"
#include "nbt/most_input.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace gui {

const char *screenName(Screen screen) {
    static const char *const names[] = {
        "loading",       "apps",   "settings",     "pairing",        "review",    "confirm",
        "progress",      "result", "error",        "choice",         "uninstall", "waiting",
        "phone-confirm", "phones", "phone-forget", "restart-idrive", "status",    "gamepad-test"};
    return static_cast<unsigned>(screen) < sizeof(names) / sizeof(names[0]) ? names[screen]
                                                                            : "unknown";
}

void UiView::setErrorMessage(const char *message) {
    *this = UiView();
    screen = Error;
    snprintf(title, sizeof(title), "NBT MANAGER");
    snprintf(lines[0], sizeof(lines[0]), "%s", message);
    lineCount = 1;
}

UiController::UiController()
    : screen(Loading), interfaceName(), port(0), version(), apps(), appCount(0), appStatus(),
      running(), app(), target(), stopping(0), settingsApp(), liveSettings(0), settings(),
      settingIndex(0), settingValue(), holding(0), holdIndex(0), holdMs(0), holdFired(0),
      clockMs(0), focus(0), status(), review(), recovery(0), host(), message(), detail(), notice(),
      captureNote(), queued(0), outstanding(0), fatal(0), exitRequested(0), handover(0),
      nextPollMs(0), pairingDeadlineMs(0), pairingLeftS(0), connected(0), loggedScreen(Loading),
      loggedConnected(0), wifiSsid(), facts(), wifiPass() {
}

UiController::~UiController() {
    core::secureClear(host, sizeof(host));
}

void UiController::clearSecrets() {
    wifiPass.wipe();
    core::secureClear(host, sizeof(host));
    core::secureClear(wifiSsid, sizeof(wifiSsid));
}

void UiController::go(Screen nextScreen) {
    if ((screen == Pairing || screen == Waiting) && nextScreen != Pairing &&
        nextScreen != Waiting) {
        clearSecrets();
    }
    screen = nextScreen;
    focus = 0;
    holding = 0;
}

void UiController::mainMenu(size_t index) {
    go(Apps);
    focus = index;
    app[0] = 0;
    memset(&review, 0, sizeof(review));
    if (!queued) {
        queued = R_RUNNING;
    }
}

bool UiController::managerSettings() const {
    return !strcmp(settingsApp, "manager");
}

size_t UiController::appIndex(const char *name) const {
    for (size_t i = 0; i < appCount; ++i) {
        if (!strcmp(apps[i], name)) {
            return i;
        }
    }
    return appCount;
}

bool UiController::stopEntry() const {
    const size_t i = appIndex(settingsApp);
    return !managerSettings() && i < appCount && running[i];
}

void UiController::leaveSettings() {
    const size_t i = managerSettings() ? appCount : appIndex(settingsApp); // Settings is last
    mainMenu(i <= appCount ? i : 0);
    message[0] = 0;
}

void UiController::leaveSession(const char *msg) {
    mainMenu(appCount);
    queued = R_CANCEL;
    ViewBuilder::formatText(message, sizeof(message), "%s", msg);
}

void UiController::showResult(const char *msg, const char *detailText) {
    go(Result);
    ViewBuilder::formatText(message, sizeof(message), "%s", msg);
    ViewBuilder::formatText(detail, sizeof(detail), "%s", detailText ? detailText : "");
}

void UiController::fail(int isFatal, const char *msg) {
    go(Error);
    fatal = isFatal;
    queued = R_NONE;
    stopping = 0;
    if (isFatal) {
        outstanding = R_NONE;
    }
    ViewBuilder::formatText(message, sizeof(message), "%s", msg);
    detail[0] = 0;
}

size_t UiController::itemCount() const {
    switch (screen) {
    case Apps:
        return appCount + 1;
    case Settings: {
        const size_t n = managerSettings() ? settings.count + 3 : uninstallItem() + 1;
        return n ? n : 1;
    }
    case Choice:
        return settingIndex < settings.count ? settings.items[settingIndex].choice_count : 0;
    case Pairing:
    case Result:
    case Error:
    case Status: // Test the gamepad
        return 1;
    case Waiting:
    case PhoneConfirm:
    case PhoneForget:
    case RestartIdrive:
    case Confirm:
    case Uninstall:
        return 2;
    case Phones:
        return phoneCount + 1;
    case Review: // "Install, even if older" only for an application update
        return review.installed[0] && strcmp(review.app, "manager") ? 3 : 2;
    default:
        return 0;
    }
}

bool UiController::inputBusy() const {
    return queued || (outstanding && !(outstanding == R_RUNNING && screen == Apps));
}

core::Status UiController::init(const char *interfaceValue, unsigned portNumber) {
    if (!interfaceValue || !nbt_token_valid(interfaceValue, sizeof(interfaceName) - 1, 0) ||
        portNumber > 65535) {
        return core::Status::error(EINVAL, "invalid session interface");
    }
    clearSecrets();
    memset(apps, 0, sizeof(apps));
    memset(appStatus, 0, sizeof(appStatus));
    memset(running, 0, sizeof(running));
    memset(app, 0, sizeof(app));
    memset(target, 0, sizeof(target));
    memset(settingsApp, 0, sizeof(settingsApp));
    memset(settingValue, 0, sizeof(settingValue));
    memset(message, 0, sizeof(message));
    memset(detail, 0, sizeof(detail));
    memset(notice, 0, sizeof(notice));
    settings = nbt_settings();
    status = WorkerStatus();
    review = WorkerStatus();
    appCount = 0;
    stopping = 0;
    liveSettings = 0;
    settingIndex = 0;
    holding = 0;
    holdIndex = 0;
    holdMs = 0;
    holdFired = 0;
    clockMs = 0;
    focus = 0;
    recovery = 0;
    queued = R_LIST;
    outstanding = R_NONE;
    fatal = 0;
    exitRequested = 0;
    handover = 0;
    nextPollMs = 0;
    pairingDeadlineMs = 0;
    pairingLeftS = 0;
    connected = 0;
    strcpy(interfaceName, interfaceValue);
    port = portNumber;
    strcpy(version, "dev");
    screen = Loading;
    return core::Status();
}

void UiController::setVersion(const char *value) {
    if (value && nbt_token_valid(value, kVersionMax, NBT_TOKEN_UPPER | NBT_TOKEN_DOT)) {
        strcpy(version, value);
    } else {
        strcpy(version, "dev");
    }
}

core::Status UiController::view(UiView &viewRef) const {
    return ViewBuilder::build(*this, viewRef);
}

core::Status UiController::pairingLink(core::Span<char> output) const {
    char *out = output.data();
    const size_t size = output.size();
    if (!out || !size || screen != Pairing || !hasWifi()) {
        return core::Status::error(EPERM, "Wi-Fi code unavailable");
    }
    return wifiCodeText(wifiSsid, wifiPass.data(), output);
}

void UiController::logChanges() {
    if (screen != loggedScreen) {
        nbt_logf("screen %s -> %s%s%s", screenName(loggedScreen), screenName(screen),
                 message[0] ? ": " : "", message);
        loggedScreen = screen;
    }
    if (screen == Pairing && connected != loggedConnected) {
        nbt_logf(connected ? "paired phone upload received" : "pairing started");
        loggedConnected = connected;
    } else if (screen != Pairing) {
        loggedConnected = 0;
    }
}

void UiController::handoverFailed() {
    appRun = 0;
    ViewBuilder::formatText(message, sizeof(message), "%s did not appear", target);
}

void UiController::setNotice(const char *value) {
    ViewBuilder::formatText(notice, sizeof(notice), "%s", value ? value : "");
}

bool UiController::installing() const {
    return screen == Progress || outstanding == R_APPROVE || queued == R_APPROVE;
}

int UiController::home() {
    if (installing()) {
        return 0;
    }
    exitRequested = 1;
    return 1;
}

int UiController::readInputFd(int fd, bool apply) {
    static const struct {
        unsigned char code;
        const char *name;
    } kNames[] = {{NBT_INPUT_UP, "up"},         {NBT_INPUT_DOWN, "down"},
                  {NBT_INPUT_SELECT, "select"}, {NBT_INPUT_BACK, "back"},
                  {NBT_INPUT_HOME, "menu"},     {NBT_INPUT_RELEASE, "release"}};
    unsigned char byte = 0;
    ssize_t n = 0;
    int count = 0;
    while ((n = read(fd, &byte, 1)) == 1) {
        const char *name = "unknown";
        for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
            if (kNames[i].code == byte) {
                name = kNames[i].name;
            }
        }
        ++count;
        nbt_logf("input %s%s", name, apply ? "" : " (ignored: hidden)");
        if (!apply) {
            continue;
        }
        if (byte == NBT_INPUT_UP || byte == NBT_INPUT_DOWN || byte == NBT_INPUT_SELECT ||
            byte == NBT_INPUT_BACK) {
            press(byte == NBT_INPUT_UP       ? KeyUp
                  : byte == NBT_INPUT_DOWN   ? KeyDown
                  : byte == NBT_INPUT_SELECT ? KeySelect
                                             : KeyBack);
        } else if (byte == NBT_INPUT_HOME) {
            home();
        } else if (byte == NBT_INPUT_RELEASE) {
            release(KeySelect);
        }
    }
    return n == 0 ? -1 : count;
}

} // namespace gui
} // namespace nbt
