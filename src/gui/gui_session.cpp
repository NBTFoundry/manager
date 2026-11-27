// SPDX-License-Identifier: MPL-2.0
/* Interactive manager GUI session and event loop; a client of the worker. */
#include "nbt/gui/gui_session.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/core/system.hpp"
#include "nbt/display/layer_manager.hpp"
#include "nbt/gui/qr_code.hpp"
#include "nbt/gui/shortcut_ui.hpp"
#include "nbt/gui/ui_renderer.hpp"
#include "nbt/log.h"
#include "nbt/most_input.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace gui {
static const uint64_t kPreviewMs = 15000u, kHealthCheckMs = 45000u,
                      kEscapeHoldMs = NBT_CAPTURE_ESCAPE_HOLD_MS;

GuiSession::GuiSession()
    : config_(), client_(), channel_(client_), ui_(), view_(), shown_(), screen_(), volumeOsd_(),
      panel_(), plugin_(), capture_(), gamepad_(), language_(), theme_(), touchSocket_(),
      touchFrames_(), touchTracker_(60, 24, 320), nowMs_(0), deadlineMs_(0), heartbeatMs_(0),
      backHoldMs_(0), pacer_(), inputFd_(-1), hidden_(true), updating_(false),
      exitReason_("error") {
}

GuiSession::~GuiSession() {
    closeSession();
}

int GuiSession::init(const GuiSessionConfig &config) {
    config_ = config;
    if (!ui_.init(config_.interfaceName, config_.port).ok()) {
        return -1;
    }
    ui_.setVersion(config_.version);
    return 0;
}

int GuiSession::drawUi(const UiView &view) {
    char link[256] = {0};
    QrCode qr;
    const bool withQr =
        view.screen == Pairing && ui_.pairingLink(core::Span<char>(link, sizeof(link))).ok() &&
        qr.encode(reinterpret_cast<const unsigned char *>(link), strlen(link), -1).ok();
    UiRenderer renderer;
    const int rc =
        renderer.render(view, withQr ? &qr : NULL, screen_.frame()).ok() && screen_.post().ok()
            ? 0
            : -1;
    core::secureClear(link, sizeof(link));
    return rc;
}

int GuiSession::drawFrame(const UiView &view) {
    const core::Result<uint64_t> before = core::monotonicMs();
    if (drawUi(view)) {
        return -1;
    }
    const core::Result<uint64_t> after = core::monotonicMs();
    if (!before.get() || !after.get()) {
        return 0;
    }
    pacer_.drawn(before.value(), after.value());
    return 0;
}

// The launcher starts the worker and passes its channel; test builds may spawn
// one instead. The GUI never restarts it: a lost channel ends the GUI and the
// launcher restarts both.
core::Status GuiSession::startWorker() {
    core::Status status = core::Status::error(ENOTCONN, "no worker channel");
    if (config_.workerFd >= 0) {
        const int fd = config_.workerFd;
        config_.workerFd = -1;
        status = client_.attach(fd);
    }
#ifdef NBT_TESTING
    else if (config_.testWorker) {
        status = client_.spawnTest(config_.testWorker, config_.root);
    }
#endif
    if (!status.ok()) {
        return status;
    }
    nbt_logf("worker connected, pid %d", static_cast<int>(client_.pid())); // before its thread
    return channel_.start();
}

int GuiSession::start(bool &workerStarted) {
    const core::Result<uint64_t> startMs = core::monotonicMs();
    language_.start(config_.root, config_.languagePath, ui_); // before any text is shown
    theme_.start(config_.root, config_.makePath);
    if (config_.recovered) {
        ui_.setNotice(text(T_RECOVERED));
    }
    errno = 0; // 0 in the log: the check failed without a system call's error
    if (!startMs.get() || !capture_.start(config_.capturePath, startMs.value()) ||
        !plugin_.start(config_.signalPath, config_.livenessPath, startMs.value())) {
        nbt_logf("liveness file %s: errno %d", config_.livenessPath, errno);
        return -1;
    }
    core::Status status = screen_.open(640, 240, display::kManagerSurface);
    if (!status.ok()) {
        nbt_logf("screen window: %s (errno %d)", status.message(), status.code());
        // dev79, 33 s after a boot: LayerManager answered nothing for 28 s, and
        // each restart held iDrive frozen again. One attempt; the driver retries.
        exitReason_ = status.code() == ENODEV ? "LayerManager did not answer" : exitReason_;
        return status.code() == ENODEV ? 1 : -1;
    }
    shown_.setErrorMessage(text(T_STARTING));
    if (drawUi(shown_)) { // the first post makes LayerManager create the surface
        nbt_logf("first frame failed");
        return -1;
    }
    (void)volumeOsd_.start(static_cast<int>(screen_.targetWidth()),
                           static_cast<int>(screen_.targetHeight()));
    // Hidden, input ignored, until the plugin says iDrive is held on its
    // inert state: never over iDrive's menus, the camera or a warning.
    nbt_logf("window ready; shown once iDrive holds its focus");
    status = startWorker();
    workerStarted = status.ok();
    if (!workerStarted) {
        nbt_logf("worker unavailable: %s (errno %d)", status.message(), status.code());
    }
    return workerStarted ? 0 : -1;
}
int GuiSession::servicePanel(bool &shouldExit) {
    const display::Placement::State state = panel_.service(nowMs_);
    volumeOsd_.service(nowMs_, panel_);
    const bool failed = state == display::Placement::Failed;
    const bool lost = failed || state == display::Placement::Lost;
    const bool panelConfirmed =
        panel_.onPanel(display::kManagerSurface) || panel_.onPanel(display::kAppSurface);
    if (panelConfirmed && !lost && !ui_.installing() && nbt_most_input_back_key_held()) {
        if (!backHoldMs_) {
            backHoldMs_ = nowMs_ ? nowMs_ : 1u;
        } else if (nowMs_ >= backHoldMs_ + kEscapeHoldMs) {
            backHoldMs_ = 0;
            capture_.latchEscape();
            if (ui_.target[0] && (panel_.active(display::kAppSurface) || ui_.appRun)) {
                nbt_logf("forced escape from %s; stopping", ui_.target);
                ui_.abortApp(T_STOPPING_APP);
                flushEscapeStop(channel_, ui_);
            } else {
                nbt_logf("forced escape; closing");
            }
            exitReason_ = "forced escape";
            shouldExit = true;
            return 0;
        }
    } else {
        backHoldMs_ = 0;
    }
    const bool ended = ui_.takeAppEnded();
    if (panel_.surface() == display::kAppSurface && (ended || lost)) {
        nbt_logf("%s %s; the manager is shown again", ui_.target,
                 failed ? "did not appear" : (lost ? "left the panel" : "ended"));
        if (failed) {
            ui_.abortApp(T_APP_NOT_SHOWN);
        }
        return showManager();
    }
    if (!lost) {
        capture_.sync(inputFd_ >= 0 && panelConfirmed, nowMs_);
        // The note reaches the list through gatherStatus.
        (void)captureWatch_.poll(config_.diagPath, capture_.generation(),
                                 inputFd_ >= 0 && panelConfirmed, nowMs_);
        return 0;
    }
    capture_.setOff();
    if (!ui_.installing()) {
        exitReason_ =
            failed ? "the manager could not take the panel" : "another surface took the panel";
        shouldExit = true;
        return 0;
    }
    panel_.clear();
    hidden_ = true;
    return screen_.setVisible(false).ok() ? 0 : -1;
}

int GuiSession::stepWorkerAndUi() {
    if (!channel_.connected() && !channel_.busy()) { // a failed reply is taken first
        nbt_logf("worker channel lost; closing so the launcher restarts both");
        exitReason_ = "worker channel lost";
        return -1;
    }
    const bool live = panel_.onPanel(display::kManagerSurface);
    if (inputFd_ >= 0) {
        const int inputs = ui_.readInputFd(inputFd_, live);
        if (inputs < 0) {
            close(inputFd_);
            inputFd_ = -1;
        } else if (inputs > 0 && live) {
            deadlineMs_ = nowMs_ + kIdleMs;
        }
    }
    if (gamepad_.poll(ui_, nowMs_, live)) {
        deadlineMs_ = nowMs_ + kIdleMs;
    }
    serviceTouch();
    shortcutStartPoll(ui_, nowMs_);
    gatherStatus(ui_, captureWatch_, gamepad_, inputFd_ >= 0);
    language_.poll(ui_, nowMs_);
    theme_.poll(nowMs_);
    ui_.pump(channel_);
    if (gamepad_.takeHandover()) {
        handOver();
    }
    ui_.logChanges();
    if (!ui_.view(view_).ok()) {
        return -1;
    }
    // A change waits for the next frame slot (waitInput wakes for it).
    if (!hidden_ && view_ != shown_ && pacer_.due(nowMs_)) {
        if (drawFrame(view_)) {
            return -1;
        }
        shown_ = view_;
    }
    return 0;
}

int GuiSession::run(int inputFd, const volatile sig_atomic_t &stopping) {
    inputFd_ = inputFd;
    volumeOsd_.attachInput();
    gamepad_.start(config_.root);
    const core::Result<uint64_t> startNow = core::monotonicMs();
    if (!startNow.get()) {
        return 1;
    }
    nowMs_ = heartbeatMs_ = startNow.value();
    display::setWaitHook(&GuiSession::beatWhileWaiting, this);
    deadlineMs_ =
        nowMs_ + (inputFd_ >= 0 ? kIdleMs : (config_.readyFd >= 0 ? kHealthCheckMs : kPreviewMs));
    if (showHandedOver()) {
        return 1;
    }
    while (!stopping && !ui_.wantsExit()) {
        const core::Result<uint64_t> now = core::monotonicMs();
        if (!now.get() || !plugin_.beat(now.value())) {
            return 1;
        }
        nowMs_ = now.value();
        bool shouldExit = false;
        if (pollHoldSignal(shouldExit) || (!shouldExit && servicePanel(shouldExit)) ||
            (!shouldExit && stepWorkerAndUi())) {
            return 1;
        }
        if (shouldExit) {
            break;
        }
        if (endForUpdate()) {
            break;
        }
        if ((ui_.updating() || panel_.active(display::kAppSurface)) &&
            deadlineMs_ < nowMs_ + kIdleMs) {
            deadlineMs_ = nowMs_ + kIdleMs;
        }
        if (nowMs_ >= deadlineMs_) {
            exitReason_ = inputFd_ >= 0 ? "idle" : "preview ended";
            break;
        }
        if (sendReadyHeartbeat() || waitInput()) {
            return 1;
        }
    }
    exitReason_ = stopping ? "signal" : (ui_.wantsExit() ? "closed by the user" : exitReason_);
    return 0;
}

void GuiSession::closeSession() {
    gamepad_.stop();
    capture_.setOff();
    ui_.clearSecrets();
    if (inputFd_ >= 0) {
        close(inputFd_);
        inputFd_ = -1;
    }
    panel_.clear();
    display::setWaitHook(NULL, NULL);
    volumeOsd_.stop();
    screen_.close();
    channel_.stop();
    client_.close();
}

} // namespace gui
} // namespace nbt
