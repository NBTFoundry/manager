// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_UI_CONTROLLER_HPP
#define NBT_GUI_UI_CONTROLLER_HPP

#include "nbt/core/secure.hpp"
#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/gui/files_state.hpp"
#include "nbt/gui/status_page.hpp"
#include "nbt/gui/text.hpp"
#include "nbt/gui/ui_view.hpp"
#include "nbt/gui/wifi_code.hpp"
#include "nbt/process.h"
#include "nbt/settings.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace nbt {
namespace gui {

static const size_t kReplyMax = 2048;
static const size_t kCommandMax = 160;
static const unsigned kPollMs = 1000;
static const unsigned kPairingMs = 60000;
static const unsigned kHoldMs = 1500;
static const unsigned kHoldShowMs = 500;
static const size_t kVersionMax = 23;
/* An installed candidate counts as healthy after NBT_HEALTHY_MS ready. */
static const unsigned kAppHealthMs = NBT_HEALTHY_MS;

/* Worker STATUS state numbers; see PROTOCOL.md. */
enum WorkerStateCode {
    W_IDLE,
    W_SESSION,
    W_VERIFIED,
    W_INSTALLING,
    W_INSTALLED,
    W_FAILED,
    W_OPENED
};

enum RequestKind {
    R_NONE,
    R_LIST,
    R_RUNNING,
    R_SESSION,
    R_STATUS,
    R_START,
    R_STOP,
    R_APPROVE,
    R_CANCEL,
    R_SETTINGS,
    R_SET,
    R_REMOVE,
    R_UNBLOCK,
    R_SERVICES,
    R_PHONE_STATUS,
    R_PHONE_OPEN,
    R_PHONE_YES,
    R_PHONE_NO,
    R_PHONES,
    R_PHONE_FORGET,
    R_SETUP, // after R_PHONE_*: replies go through phoneReply, then setupReply
    R_IDRIVE_RESTART,
    R_SSH_ENABLE, // SSH ENABLE: toolsReply starts sshd, telnetd and qconn until a restart
    R_LANGUAGE,   // SET manager language: languageReply, after setupReply
    R_AUTOSTART,  // AUTOSTART <app> on|off: autostartReply (autostart_ui.hpp), after setupReply
    R_SHORTCUT,   // SET manager shortcut<n>: shortcutReply (shortcut_ui.hpp), after setupReply
    R_FILES,      // FILES: filesReply (files_ui.hpp), after setupReply
    R_FILES_DELETE,
    R_FILES_SCAN,
    R_FILES_USB,
    R_FILES_IMPORT
};

/* The manager's Settings list: its declared settings, then these. */
enum ManagerEntry {
    EntryLanguage,
    EntryPair,
    EntryPhones,
    EntryStatus,
    EntryTools, // the Tools menu (tools_ui.hpp): restart the head unit, enable SSH
    kManagerEntries
};

class WorkerChannel;

struct WorkerStatus {
    int state;
    char app[kIdMax + 1];
    char installed[kIdMax + 1];
    char proposed[kIdMax + 1];
    uint64_t sequence;
    char digest[65];
    char message[128];
    char developer[33]; // the developer key's name; empty for the apps key
    bool clearsCodes;   // its diagnostics policy may clear fault codes
};

struct Request {
    char command[kCommandMax];
    unsigned timeoutMs;
};

class UiController {
public:
    UiController();
    ~UiController();
    UiController(const UiController &) = delete;
    UiController &operator=(const UiController &) = delete;

    core::Status init(const char *interfaceName, unsigned port);
    int press(Key key);
    int release(Key key);
    int readInputFd(int fd, bool apply);
    void setVersion(const char *version);
    int nextRequest(uint64_t nowMs, Request &request);
    int reply(const char *reply, uint64_t nowMs);
    void requestFailed();
    /* Takes the worker's reply when it is there, else submits the next request. */
    void pump(WorkerChannel &channel);
    core::Status view(UiView &view) const;
    core::Status pairingLink(core::Span<char> output) const;
    bool wantsExit() const {
        return exitRequested != 0;
    }
    /* True once after START answered "OK screen": `target` is to have the panel. */
    bool takeHandover() {
        const bool requested = handover != 0;
        handover = 0;
        appRun = requested ? 1 : appRun;
        return requested;
    }
    /* True once when the application handed the panel was seen running in
     * RUNNING and then no longer is. */
    bool takeAppEnded() {
        const bool ended = appRun == 3;
        appRun = ended ? 0 : appRun;
        return ended;
    }
    void handoverFailed();
    /* Aborts a running or failed screen application: returns to Apps and stops
     * `target`, saying `why` ({1}: the application). */
    void abortApp(TextId why);
    /* Logs screen changes without credentials or request bodies. */
    void logChanges();
    void setNotice(const char *notice);
    int home();
    bool installing() const;
    /* An install or update under way, from the session to the installation:
     * the GUI does not close for idling meanwhile. */
    bool updating() const {
        return installing() || screen == Pairing || screen == Waiting || screen == PhoneConfirm ||
               screen == Review || screen == Confirm ||
               (screen == UsbFiles && files.usbPhase == UsbImporting);
    }
    static int parseStatus(const char *reply, WorkerStatus &status);
    /* The car's Wi-Fi Direct group from SESSION; only Pair a phone shows its Wi-Fi QR. */
    bool setWifi(const char *ssid, const char *pass) {
        if (!wifiSsidValid(ssid) || !wifiPassValid(pass) || !wifiPass.set(pass)) {
            return false;
        }
        memcpy(wifiSsid, ssid, strlen(ssid) + 1);
        return true;
    }
    bool hasWifi() const {
        return wifiSsid[0] != 0;
    }
    void clearSecrets();

    void go(Screen nextScreen);
    void mainMenu(size_t index);
    bool managerSettings() const;
    size_t managerEntry(ManagerEntry entry) const {
        return settings.count + static_cast<size_t>(entry);
    }
    /* Shows the language the setting and iDrive ask for (nbt/model/language.hpp). */
    void applyLanguage();
    size_t appIndex(const char *name) const;
    /* An application's page: its declared settings, Start with the car
     * (autostart_ui.hpp), App shortcut (shortcut_ui.hpp), then Stop while it
     * runs, Unblock, Files, Uninstall. */
    size_t autostartItem() const {
        return settings.count;
    }
    size_t shortcutItem() const {
        return settings.count + 1;
    }
    size_t stopItem() const {
        return settings.count + (managerSettings() ? 0u : 2u);
    }
    bool stopEntry() const;
    bool unblockEntry() const {
        return !managerSettings() && !strcmp(settingsApp, quarantinedApp);
    }
    size_t unblockItem() const {
        return stopItem() + (stopEntry() ? 1u : 0u);
    }
    /* The Settings item for settingsApp's files (capability `files`), then the
     * one that uninstalls it (neither offered for the manager). */
    bool filesEntry() const {
        return !managerSettings() && files.offered;
    }
    size_t filesItem() const {
        return unblockItem() + (unblockEntry() ? 1u : 0u);
    }
    size_t uninstallItem() const {
        return filesItem() + (filesEntry() ? 1u : 0u);
    }
    void leaveSettings();
    void leaveSession(const char *msg);
    void showResult(const char *msg, const char *detailText, int resultKind = 0);
    void fail(int isFatal, const char *msg);
    size_t itemCount() const;
    bool inputBusy() const;

    Screen screen;
    char interfaceName[16];
    unsigned port;
    char version[kVersionMax + 1];
    char apps[kAppsMax][kIdMax + 1];
    size_t appCount;
    char appStatus[kAppsMax][kAppStatusMax + 1];
    unsigned char running[kAppsMax];
    unsigned char service[kAppsMax] = {}; // only its service runs (SERVICES)
    // Application `i` or its service runs: holding the push stops them.
    bool runs(size_t i) const {
        return i < appCount && (running[i] || service[i]);
    }
    unsigned char needsUpdate[kAppsMax] = {}; // disabled until updated (LIST "<app>|update")
    char app[kIdMax + 1];
    char target[kIdMax + 1];
    int stopping;
    char settingsApp[kIdMax + 1];
    int liveSettings;
    nbt_settings settings;
    size_t settingIndex;
    char settingValue[NBT_SETTING_CHOICE_MAX + 1];
    int holding;
    size_t holdIndex;
    uint64_t holdMs;
    int holdFired;
    uint64_t clockMs;
    size_t focus;
    WorkerStatus status;
    WorkerStatus review;
    int recovery;
    char host[32];
    char message[kTextMax + 1];
    char detail[2 * kTextMax + 2]; // the worker's reason, shown on up to two lines
    char notice[kTextMax + 1];
    char captureNote[kTextMax + 1];      // CaptureWatch: when iDrive gets the keys too
    char captureSeen[kTextMax + 1] = {}; // the capture note last closed on the main menu
    int queued;
    int outstanding;
    int fatal;
    int exitRequested;
    int handover;
    int appRun = 0;        // the handed-over application: 1 started, 2 seen running, 3 ended
    uint64_t healthMs = 0; // when the installed candidate's health check began
    uint64_t nextPollMs;
    uint64_t pairingDeadlineMs;
    unsigned pairingLeftS;
    int connected;
    Screen loggedScreen;
    int loggedConnected;
    char wifiSsid[33];
    char phoneAction[33] = {};
    char phoneName[33] = {};
    char phoneCode[7] = {};
    char phoneApp[49] = {};
    bool phonePair = false;
    // A developer key the phone asks to add ('k') or remove ('u'), shown on
    // PhoneConfirm with its name and fingerprint; 0 for pairing.
    char phoneKey = 0;
    char keyName[33] = {};
    char keyPrint[20] = {};
    char phoneIds[4][65] = {};
    char phoneNames[4][33] = {};
    size_t phoneCount = 0;
    size_t phoneSelected = 0;
    int pairRequested = 0;  // Settings > Pair a phone: open the Wi-Fi code once the session is up
    int setupDue = 1;       // SETUP once the list shows; again while an update is on trial
    uint64_t setupAtMs = 0; // not before
    uint64_t trialSinceMs = 0; // this manager runs on trial since (SETUP "trial"), 0: not
    int restartFromTools = 0;  // the restart confirmation came from Tools (tools_ui.hpp)
    int resultKind = 0;        // the Tools request whose result the Result screen shows, else 0
    int faultStop = 0;
    char quarantinedApp[kIdMax + 1] = {};
    char stopNote[kTextMax + 1] = {};          // "Stopping <app>" while that is the message shown
    int languageChoice = model::kLanguageAuto; // manager/language, as loaded or saved
    int carLanguage = -1;                      // iDrive's language as a model::Language; -1 unknown
    int languagePick = -1;                     // the choice being saved (R_LANGUAGE)
    int autostart = 0;       // settingsApp starts with the car (SETTINGS <app> live)
    int autostartPaused = 0; // ...but is paused after failed automatic starts
    int autostartPick = 0;   // the choice being saved (R_AUTOSTART)
    int shortcut = 0;        // settingsApp's App shortcut entry, 0 none (+shortcut<n>)
    int shortcutPick = 0;    // the entry being saved (R_SHORTCUT)
    int settingsUpdate = 0;  // settingsApp is disabled until updated (SETTINGS "+update")
    StatusFacts facts;       // Settings > Status and the gamepad test (gatherStatus)
    // An application's files (files_ui.hpp). Not "= {}": GCC 4.8 (QNX) flags that
    // with missing-field-initializers.
    FilesState files = FilesState();

private:
    core::SecureString<63> wifiPass;
};

} // namespace gui
} // namespace nbt

#endif
