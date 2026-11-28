// SPDX-License-Identifier: MPL-2.0
// Focus events from the plugin's sender thread (focus_events.hpp).
#include "focus_events.hpp"

#include "framework.hpp"
#include "hmi_threads.hpp"
#include "nbt/gui/focus_sender.hpp"
#include "nbt/gui/hmi_hold.hpp"
#include "nbt/log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace nbt {
namespace plugin {

namespace {
nbt::gui::FocusSender sender;
bool threaded = false;

uint64_t clockMs() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
        return 0;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

void fire(bmw::PluginApp *app, unsigned event) {
    const uint64_t start = clockMs();
    nbt_logf("focus event %u begin", event);
    app->triggerEvent(event);
    nbt_logf("focus event %u returned after %u ms", event,
             static_cast<unsigned>(clockMs() - start));
}

void fireOnSender(void *app, unsigned event) {
    static bool named = false;
    if (!named) {
        named = true;
        nameThread("nbt focus");
    }
    fire(static_cast<bmw::PluginApp *>(app), event);
}

void onStuck(void *, unsigned event, unsigned elapsedMs) {
    static bool named = false;
    if (!named) {
        named = true;
        nameThread("nbt focus watch");
    }
    nbt_logf("focus event %u has not returned after %u ms", event, elapsedMs);
    char why[48];
    snprintf(why, sizeof(why), "focus event %u, %u ms", event, elapsedMs);
    logHmiThreads(why);
}
} // namespace

void startFocusEvents(bmw::PluginApp *app) {
    threaded = sender.start(&fireOnSender, &onStuck, &clockMs, app);
    if (!threaded) {
        nbt_logf("focus sender not started; focus events from the plugin thread");
    }
}

void stopFocusEvents() {
    if (threaded) {
        sender.stop();
        threaded = false;
    }
}

void focusEvent(bmw::PluginApp *app, unsigned event) {
    if (!threaded) {
        fire(app, event);
        return;
    }
    if (!sender.send(event)) {
        uint64_t since = 0;
        const unsigned current = sender.current(since);
        nbt_logf("focus event %u dropped: event %u in iDrive for %u ms", event, current,
                 current ? static_cast<unsigned>(clockMs() - since) : 0u);
    }
}

bool focusStuck() {
    return threaded && sender.stuck();
}

void holdFocusEvents(bmw::PluginApp *app, unsigned actions) {
    if (actions & nbt::gui::HoldReopen) {
        focusEvent(app, kReleaseEvent);
        focusEvent(app, kOpenEvent);
    }
    if (actions & nbt::gui::HoldToParent) {
        focusEvent(app, kOpenEvent);
    }
    if (actions & nbt::gui::HoldFocus) {
        focusEvent(app, kHoldEvent);
    }
    if (actions & nbt::gui::HoldRelease) {
        focusEvent(app, kReleaseEvent);
    }
}

void holdActionsText(unsigned actions, char *out, size_t size) {
    static const struct {
        unsigned bit;
        const char *text;
    } kNames[] = {
        {nbt::gui::HoldSpawn, " spawn"},     {nbt::gui::HoldReopen, " reopen (list, then open)"},
        {nbt::gui::HoldToParent, " open"},   {nbt::gui::HoldFocus, " hold"},
        {nbt::gui::HoldRelease, " release"}, {nbt::gui::HoldAside, " aside"},
        {nbt::gui::HoldShow, " show"}};
    if (!out || !size) {
        return;
    }
    out[0] = 0;
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
        if ((actions & kNames[i].bit) && strlen(out) + strlen(kNames[i].text) < size) {
            strcat(out, kNames[i].text);
        }
    }
}

} // namespace plugin
} // namespace nbt
