// SPDX-License-Identifier: MPL-2.0
/* Interactive manager GUI session and event loop; a client of the worker. */
#ifndef NBT_GUI_GUI_SESSION_HPP
#define NBT_GUI_GUI_SESSION_HPP

#include "nbt/ccs.h"
#include "nbt/display/placement.hpp"
#include "nbt/gui/capture_link.hpp"
#include "nbt/gui/capture_watch.hpp"
#include "nbt/gui/frame_pacer.hpp"
#include "nbt/gui/gamepad_input.hpp"
#include "nbt/gui/language_watch.hpp"
#include "nbt/gui/plugin_link.hpp"
#include "nbt/gui/screen_window.hpp"
#include "nbt/gui/theme.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/volume_osd.hpp"
#include "nbt/gui/worker_channel.hpp"
#include "nbt/gui/worker_client.hpp"

#include <signal.h>
#include <stdint.h>

namespace nbt {
namespace gui {

static const uint64_t kIdleMs = 120000u; // the shown manager closes after this without input

struct GuiSessionConfig {
    const char *root;
    const char *version;
    const char *interfaceName;
    unsigned port;
    int slot;
    int readyFd;
    int workerFd; // the launcher's worker channel (--worker-fd)
    const char *testWorker;
    const char *signalPath;
    const char *livenessPath;
    const char *capturePath;
    const char *diagPath; // the capture shim's ring (CaptureWatch); NULL: not watched
    bool recovered;
    const char *languagePath; // iDrive's language from the plugin (car_language.hpp); NULL: none
    const char *makePath;     // the car's CarPlay configuration (model/theme.hpp); NULL: none
    bool handover;            // started right after a GUI that ended for a manager update
};

class GuiSession {
public:
    GuiSession();
    ~GuiSession();

    int init(const GuiSessionConfig &config);
    void setCaptureController(const nbt_ccs_config &ccs) {
        capture_.setController(ccs);
    }
    void setNotice(const char *notice) {
        ui_.setNotice(notice);
    }
    /* 0 when started; 1 when LayerManager gave no panel (end, no restart); -1 on failure. */
    int start(bool &workerStarted);
    int run(int inputFd, const volatile sig_atomic_t &stopping);
    void closeSession();
    const char *exitReason() const {
        return exitReason_;
    }
    /* The GUI ended for an approved manager update (NBT_MANAGER_GUI_EXIT_UPDATE). */
    bool updating() const {
        return updating_;
    }
    /* Gives the liveness line to the launcher, which keeps iDrive held until
     * the new release's GUI runs (src/launcher/update_handover.cpp). */
    void handLivenessToLauncher();

private:
    GuiSession(const GuiSession &);
    GuiSession &operator=(const GuiSession &);

    int drawUi(const UiView &view);
    /* drawUi at most once per frame (30 fps), timing slow frames. */
    int drawFrame(const UiView &view);
    core::Status startWorker();
    int pollHoldSignal(bool &shouldExit);
    int showManager();
    int showHandedOver();
    bool endForUpdate();
    void handOver();
    /* iDrive took the screen from the application shown: its host pauses it
     * (nbt/process/app_pause.hpp). */
    void pauseShownApp();
    int servicePanel(bool &shouldExit);
    int stepWorkerAndUi();
    int sendReadyHeartbeat();
    /* Both heartbeats while a LayerManagerControl call blocks the loop. */
    static void beatWhileWaiting(void *session);
    int waitInput();

    GuiSessionConfig config_;
    WorkerClient client_;
    WorkerChannel channel_; // client_'s requests, on their own thread once started
    UiController ui_;
    UiView view_;
    UiView shown_;
    ScreenWindow screen_;
    VolumeOsd volumeOsd_;
    display::Placement panel_; // the manager's surface or the application's
    PluginLink plugin_;
    CaptureLink capture_;
    CaptureWatch captureWatch_;
    GamepadInput gamepad_;
    LanguageWatch language_;
    ThemeWatch theme_;
    uint64_t nowMs_;
    uint64_t deadlineMs_;
    uint64_t heartbeatMs_;
    uint64_t backHoldMs_;
    FramePacer pacer_; // 30 fps
    int inputFd_;
    bool hidden_; // the manager's own screen is not wanted on the panel
    bool updating_;
    const char *exitReason_;
};

} // namespace gui
} // namespace nbt

#endif
