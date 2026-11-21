// SPDX-License-Identifier: MPL-2.0
// Minimal PluginApp ABI bridge for the NBT EVO framework. The ABI declarations
// are reconstructed from the target libraries and require firmware validation.
//
// Selecting the entry starts the manager launcher and holds iDrive's focus on
// an inert state while the manager window is shown; see include/nbt/gui/hmi_hold.hpp.
// All callbacks, actions and posted runnables run on the app's own thread; focus
// events go out from the plugin's sender thread (focus_events.hpp).
#include "nbt/gui/hmi_hold.hpp"
#include "nbt/log.h"

#include <dlfcn.h>
#include <errno.h>
#include <new>
#include <stdint.h>
#include <string.h>

#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)-2) // QNX value
#endif

#include "capture_diag.hpp"
#include "car_data.hpp"
#include "car_language.hpp"
#include "focus_events.hpp"
#include "framework.hpp"
#include "launcher_spawn.hpp"
#include "phone_runtime.hpp"
#include "shortcut.hpp"

using nbt::plugin::focusEvent;
using nbt::plugin::guiRunning;
using nbt::plugin::logError;
using nbt::plugin::nowMs;
using nbt::plugin::spawnLauncher;

namespace {
const unsigned kOpenAction = 2;
const unsigned kBlankState = 14;
const unsigned kParentState = 15;
const unsigned kFocusHandler = 1;
const unsigned kHoldEvent = 50;
const unsigned kReleaseEvent = 51;
const unsigned kOpenEvent = 52;
const size_t kObjectBytes = 0x200;
uint64_t objectStorage[kObjectBytes / sizeof(uint64_t)];
const size_t kVtableEntries = 34;
const size_t kOnInit = 22;
const size_t kOnFinalize = 23;
void *vtable[kVtableEntries + 2];
bmw::PluginApp *instance = NULL;
uint64_t holdStorage[(sizeof(nbt::gui::HmiHold) + sizeof(uint64_t) - 1) / sizeof(uint64_t)];
nbt::gui::HmiHold *hold = NULL;
uint64_t signalSequence = 0;
// An application's hold (capability `hold`), next to the manager's own.
uint64_t appHoldStorage[(sizeof(nbt::gui::AppHold) + sizeof(uint64_t) - 1) / sizeof(uint64_t)];
nbt::gui::AppHold *appHold = NULL;
uint64_t answerSequence = 0;

struct PollRunnable {
    void **vptr;
};
PollRunnable pollRunnable;
void *pollVtable[5];
bool polling = false;
nbt::plugin::PhoneRuntime phoneRuntime;
nbt::plugin::CarLanguage carLanguage;
nbt::plugin::CarData carData;
nbt::plugin::CaptureDiagLogState captureDiagState;
bool requestOpen(bmw::PluginApp *app);

void signalGui(nbt::gui::HoldSignal signal) {
    uint64_t now = nowMs();
    signalSequence = now > signalSequence ? now : signalSequence + 1;
    if (nbt::gui::holdSignalWrite(nbt::gui::kHoldSignalPath, signalSequence, signal)) {
        logError("cannot signal the manager window", errno);
    }
}

// The focus events and the next poll, for the manager's hold and an application's.
void sendFocus(bmw::PluginApp *app, unsigned actions) {
    if (actions & nbt::gui::HoldToParent) {
        focusEvent(app, kOpenEvent);
    }
    if (actions & nbt::gui::HoldFocus) {
        focusEvent(app, kHoldEvent);
    }
    if (actions & nbt::gui::HoldRelease) {
        focusEvent(app, kReleaseEvent);
    }
    if ((actions & nbt::gui::HoldPoll) && !polling) {
        polling = true;
        app->post(reinterpret_cast<bmw::Runnable *>(&pollRunnable), nbt::gui::kHoldPollMs);
    }
}

void apply(bmw::PluginApp *app, unsigned actions) {
    if (actions & ~static_cast<unsigned>(nbt::gui::HoldPoll)) {
        nbt_logf("hold phase %d:%s%s%s%s%s%s", static_cast<int>(hold->phase),
                 (actions & nbt::gui::HoldSpawn) ? " spawn" : "",
                 (actions & nbt::gui::HoldToParent) ? " open" : "",
                 (actions & nbt::gui::HoldFocus) ? " hold" : "",
                 (actions & nbt::gui::HoldRelease) ? " release" : "",
                 (actions & nbt::gui::HoldAside) ? " aside" : "",
                 (actions & nbt::gui::HoldShow) ? " show" : "");
    }
    if (actions & nbt::gui::HoldAside) {
        signalGui(nbt::gui::SignalAside);
    }
    if (actions & nbt::gui::HoldShow) {
        signalGui(nbt::gui::SignalShow);
    }
    sendFocus(app, actions);
}

void answerApp(const char *name, nbt::gui::AppAnswer answer) {
    const uint64_t now = nowMs();
    answerSequence = now > answerSequence ? now : answerSequence + 1;
    if (nbt::gui::appHoldAnswerWrite(nbt::gui::kAppHoldAnswerDir, name, answerSequence, answer)) {
        logError("cannot answer the application's hold", errno);
    }
    nbt_logf("application hold %s: %s", name, answer == nbt::gui::AnswerHeld ? "held" : "aside");
}

// Focus events for an application's hold; its answers are written by pollAppHold.
void applyApp(bmw::PluginApp *app, unsigned actions) {
    if (actions & (nbt::gui::HoldToParent | nbt::gui::HoldFocus | nbt::gui::HoldRelease)) {
        nbt_logf("application hold %s:%s%s%s", appHold->app(),
                 (actions & nbt::gui::HoldToParent) ? " open" : "",
                 (actions & nbt::gui::HoldFocus) ? " hold" : "",
                 (actions & nbt::gui::HoldRelease) ? " release" : "");
    }
    sendFocus(app, actions);
}

void pollAppHold(bmw::PluginApp *app) {
    nbt::gui::AppRequest request;
    const uint64_t now = nowMs();
    const bool fresh = nbt::gui::appHoldRelayRead(nbt::gui::kAppHoldRelayPath, now, request);
    char name[NBT_HOLD_APP_MAX + 1];
    nbt::gui::AppAnswer answer = nbt::gui::AnswerNone;
    const unsigned actions = appHold->poll(now, fresh ? &request : NULL,
                                           hold->phase != nbt::gui::HoldIdle, name, answer);
    if (answer != nbt::gui::AnswerNone) {
        answerApp(name, answer);
    }
    applyApp(app, actions);
}

void runnableNoop(void *) {
}

// iDrive has not answered a focus event for 3 s: stop holding and send the
// manager window aside, so nothing waits hidden on a hold that never comes.
bool stuckHandled = false;
void checkStuck() {
    if (!nbt::plugin::focusStuck()) {
        stuckHandled = false;
    } else if (!stuckHandled) {
        stuckHandled = true;
        nbt_logf("iDrive does not answer; hold dropped, manager aside");
        hold->reset();
        signalGui(nbt::gui::SignalAside);
        if (appHold->active()) {
            answerApp(appHold->app(), nbt::gui::AnswerAside);
        }
        appHold->reset();
    }
}

void runnablePoll(void *) {
    polling = false;
    if (instance && hold) {
        checkStuck();
        const uint64_t now = nowMs();
        const bool running = guiRunning();
        if (phoneRuntime.doorDue(now)) {
            (void)spawnLauncher("--door", false);
        }
        if (phoneRuntime.shimCheckDue(now)) {
            (void)spawnLauncher("--shim-check", false);
        }
        if (phoneRuntime.autostartDue(now)) { // with iDrive's preloads: applications draw
            (void)spawnLauncher("--autostart");
        }
        if (phoneRuntime.poll(now, running) && !requestOpen(instance)) {
            phoneRuntime.openFailed();
        }
        apply(instance, hold->poll(now, guiRunning()));
        if (nbt::plugin::shortcutOpenDue(now)) {
            (void)requestOpen(instance);
        }
        pollAppHold(instance);
        carLanguage.poll(now);
        carData.poll(now);
        nbt::plugin::pollCaptureDiag(captureDiagState);
        if (!polling) {
            polling = true;
            instance->post(reinterpret_cast<bmw::Runnable *>(&pollRunnable),
                           nbt::plugin::shortcutPending() ? 250 : 1000);
        }
    }
}

bool requestOpen(bmw::PluginApp *app) {
    (void)nbt_log_open_path(NBT_LOG_PATH, "plugin");
    const bool running = guiRunning();
    nbt_logf("entry selected; manager %s", running ? "running" : "not running");
    unsigned actions = hold->entry(nowMs(), running);
    if ((actions & nbt::gui::HoldSpawn) && !spawnLauncher()) {
        hold->reset();
        return false;
    }
    apply(app, actions);
    return true;
}

bool openManager(void *object, const bmw::Parcel &) {
    return requestOpen(static_cast<bmw::PluginApp *>(object));
}

bool onBlankFocus(void *, const bmw::Parcel &parcel) {
    bool focused = false;
    parcel.readBool(focused);
    nbt_logf("blank state focus %d", focused ? 1 : 0);
    hold->focus(nbt::gui::HoldBlank, focused, nowMs());
    appHold->focus(nbt::gui::HoldBlank, focused, nowMs());
    return true;
}

bool onParentFocus(void *object, const bmw::Parcel &parcel) {
    bool focused = false;
    parcel.readBool(focused);
    nbt_logf("parent state focus %d", focused ? 1 : 0);
    hold->focus(nbt::gui::HoldParent, focused, nowMs());
    appHold->focus(nbt::gui::HoldParent, focused, nowMs());
    if (focused) {
        // Hold again now, not at the next poll (up to 250 ms): a second BACK or
        // a knob turn in that window took iDrive out of the manager (dev88).
        apply(static_cast<bmw::PluginApp *>(object), hold->poll(nowMs(), guiRunning()));
        pollAppHold(static_cast<bmw::PluginApp *>(object));
    }
    return true;
}

bool onInit(bmw::PluginApp *app) {
    bmw::ActionDelegate action = {app, &openManager};
    void **base = reinterpret_cast<void **>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
    if (base) {
        memcpy(pollVtable, base, 2 * sizeof(void *));
    }
    pollVtable[2] = reinterpret_cast<void *>(&runnableNoop);
    pollVtable[3] = reinterpret_cast<void *>(&runnableNoop);
    pollVtable[4] = reinterpret_cast<void *>(&runnablePoll);
    pollRunnable.vptr = pollVtable + 2;
    hold = new (holdStorage) nbt::gui::HmiHold();
    appHold = new (appHoldStorage) nbt::gui::AppHold();
    app->addHandler(kOpenAction, action);
    nbt::plugin::addHandler(app, kBlankState, kFocusHandler, &onBlankFocus);
    nbt::plugin::addHandler(app, kParentState, kFocusHandler, &onParentFocus);
    nbt::plugin::addShortcutHandlers(app);
    const bool initialized = app->onInit();
    if (initialized) {
        nbt::plugin::startFocusEvents(app);
        phoneRuntime.start(app, nowMs());
        carLanguage.start(app, nowMs());
        carData.start(app);
        polling = true;
        app->post(reinterpret_cast<bmw::Runnable *>(&pollRunnable), 1000);
    }
    return initialized;
}

void onFinalize(bmw::PluginApp *app) {
    carData.stop();
    carLanguage.stop();
    phoneRuntime.stop();
    nbt::plugin::stopFocusEvents();
    if (hold) {
        hold->~HmiHold();
        hold = NULL;
    }
    if (appHold) {
        appHold->~AppHold();
        appHold = NULL;
    }
    nbt_log_close();
    app->onFinalize();
}

bmw::PluginApp *create(bmw::IDispatcher &dispatcher, const char *name, const bmw::Bundle &params,
                       const bmw::Bundle &config) {
    if (instance) {
        return NULL;
    }
    memset(objectStorage, 0, sizeof(objectStorage));
    bmw::PluginApp *app = new (objectStorage)
        bmw::PluginApp(dispatcher, bmw::BasicString<char>(name ? name : "NbtManager"), 1, 0,
                       bmw::RHMI_INTERFACE_VERSION_1, params, config);
    void **base = *reinterpret_cast<void ***>(app);
    memcpy(vtable, base - 2, sizeof(vtable));
    vtable[2 + kOnInit] = reinterpret_cast<void *>(&onInit);
    vtable[2 + kOnFinalize] = reinterpret_cast<void *>(&onFinalize);
    *reinterpret_cast<void ***>(app) = vtable + 2;
    instance = app;
    return app;
}

void destroy(void *app) {
    if (!app || app != instance) {
        return;
    }
    instance = NULL;
    void **table = *reinterpret_cast<void ***>(app);
    reinterpret_cast<void (*)(void *)>(table[0])(app);
}
} // namespace

extern "C" {
struct PluginAppInfo {
    uint16_t major, minor;
    const char *name, *vendor;
    uint32_t interfaceVersion;
    bmw::PluginApp *(*create)(bmw::IDispatcher &, const char *, const bmw::Bundle &,
                              const bmw::Bundle &);
    void (*destroy)(void *);
};
__attribute__((visibility("default"))) PluginAppInfo pluginAppInfo = {
    1, 0, "App Manager", "NBT Manager", 1, &create, &destroy};
}
