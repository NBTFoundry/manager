#include "phone_runtime.hpp"
#include "nbt/log.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
namespace nbt {
namespace plugin {
namespace {
/* Off from dev48: firing iDrive's open event from this timer froze iDrive until
 * its watchdog restarted it (dev47, unlogged). Off, the plugin reads no car
 * data and answers every wake request "off"; the phone asks the driver to
 * open App Manager. To be turned on again only with a logged test build. */
const bool kOpenFromPhone = false;
uint64_t clockMs() {
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) ? 0
                                               : static_cast<uint64_t>(ts.tv_sec) * 1000 +
                                                     static_cast<uint64_t>(ts.tv_nsec) / 1000000;
}
bool ascii(const bmw::BasicString<unsigned short> &value, char *out, size_t size) {
    const unsigned short *p = value.data();
    if (!p) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        if (p[i] > 126 || (p[i] && p[i] < 32)) {
            return false;
        }
        out[i] = static_cast<char>(p[i]);
        if (!p[i]) {
            return true;
        }
    }
    return false;
}
bool enabled() {
    const char path[] = "/var/opt/nbt-manager/state/manager/phone-open";
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path, flags);
    if (fd < 0) {
        return false;
    }
    struct stat st;
    char text[4] = {};
    const bool ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
                    st.st_nlink == 1 && !(st.st_mode & 0077) && read(fd, text, sizeof(text)) == 3 &&
                    !memcmp(text, "on\n", 3);
    close(fd);
    return ok;
}
} // namespace
void PhoneRuntime::start(bmw::PluginApp *instance, uint64_t now) {
    app = instance;
    boot = now;
    nextSample = now;
    nextDoor = now + 15000;
    speedKnown = contextSafe = opening = false;
    speedMs = contextMs = doorHealthyMs = 0;
    guardReset = shimChecked = false;
    display.fd = -1;
    display.pid = 0;
    display.used = 0;
    if (!kOpenFromPhone) {
        return;
    }
    bmw::BasicString<char> speedName("driving.speedActual"), contextName("hmi.graphicalContext");
    bmw::CdsDelegate speedHandler = {this, &PhoneRuntime::onSpeed};
    bmw::CdsDelegate contextHandler = {this, &PhoneRuntime::onContext};
    app->cdsAddPropertyChangedEventHandler(speedName, 0, speedHandler);
    app->cdsAddPropertyChangedEventHandler(contextName, 0, contextHandler);
}
void PhoneRuntime::stop() {
    if (!app) {
        return;
    }
    display.stop();
    if (kOpenFromPhone) {
        bmw::BasicString<char> speedName("driving.speedActual"),
            contextName("hmi.graphicalContext");
        app->cdsRemovePropertyChangedEventHandler(speedName);
        app->cdsRemovePropertyChangedEventHandler(contextName);
    }
    app = NULL;
}
bool PhoneRuntime::onSpeed(void *self, const bmw::BasicString<unsigned short> &value) {
    PhoneRuntime &runtime = *static_cast<PhoneRuntime *>(self);
    char text[65];
    unsigned speed = 0;
    if (!ascii(value, text, sizeof(text)) || !gui::phoneSpeed(text, speed)) {
        runtime.speedKnown = false;
        return true;
    }
    if (!runtime.speedKnown || speed != runtime.speed) {
        nbt_logf("phone wake speed %u", speed);
    }
    runtime.speed = speed;
    runtime.speedKnown = true;
    runtime.speedMs = clockMs();
    return true;
}
bool PhoneRuntime::onContext(void *self, const bmw::BasicString<unsigned short> &value) {
    PhoneRuntime &runtime = *static_cast<PhoneRuntime *>(self);
    char text[2049];
    runtime.contextSafe = ascii(value, text, sizeof(text)) && gui::phoneContextSafe(text);
    runtime.contextMs = clockMs();
    return true;
}
bool PhoneRuntime::shimCheckDue(uint64_t now) {
    if (!app || shimChecked || now < boot || now - boot < 60000) {
        return false;
    }
    shimChecked = true;
    return true;
}
bool PhoneRuntime::doorDue(uint64_t now) {
    if (!app || now < boot || now < nextDoor) {
        return false;
    }
    nextDoor = now + 10000;
    gui::PhoneWake live;
    if (gui::phoneWakeRead(gui::kDoorLivePath, live) && now >= live.when &&
        now - live.when <= 4000) {
        if (!doorHealthyMs) {
            doorHealthyMs = now;
        }
        if (!guardReset && now >= doorHealthyMs && now - doorHealthyMs >= 60000) {
            guardReset = gui::phoneDoorReset("/dev/shmem/nbt-manager.door-starts");
        }
        return false;
    }
    doorHealthyMs = 0;
    guardReset = false;
    return gui::phoneDoorClaim("/dev/shmem/nbt-manager.door-starts");
}
bool PhoneRuntime::poll(uint64_t now, bool open) {
    if (!app) {
        return false;
    }
    if (!kOpenFromPhone) {
        gui::PhoneWake request;
        if (gui::phoneWakeRead(gui::kPhoneWakePath, request) && request.state == gui::WakeOpening) {
            request.state = gui::WakeOff;
            (void)gui::phoneWakeWrite(gui::kPhoneWakePath, request);
        }
        return false;
    }
    if (now >= nextSample) {
        (void)nbt_log_open_path(NBT_LOG_PATH, "plugin");
        bmw::BasicString<char> speedName("driving.speedActual"),
            contextName("hmi.graphicalContext");
        bmw::CdsDelegate speedHandler = {this, &PhoneRuntime::onSpeed},
                         contextHandler = {this, &PhoneRuntime::onContext};
        app->cdsGetPropertyAsync(speedName, speedHandler);
        app->cdsGetPropertyAsync(contextName, contextHandler);
        nextSample = now + 1000;
    }
    if (opening) {
        if (open || now < openingMs || now - openingMs > 5000) {
            request.state = open ? gui::WakeOpen : gui::WakeBusy;
            (void)gui::phoneWakeWrite(gui::kPhoneWakePath, request);
            opening = false;
        }
        return false;
    }
    gui::PhoneWake wake;
    if (gui::phoneWakeRead(gui::kPhoneWakePath, wake) && wake.state == gui::WakeOpen && !open) {
        wake.state = gui::WakeBusy;
        (void)gui::phoneWakeWrite(gui::kPhoneWakePath, wake);
    }
    if (!gui::phoneWakeRead(gui::kPhoneWakePath, wake) || wake.state != gui::WakeOpening ||
        now < wake.when || now - wake.when > 5000) {
        display.reset();
        return false;
    }
    const gui::PhoneWakeState gate = gui::phoneWakeRule(enabled(), speedKnown, speed, speedMs, true,
                                                        contextSafe, contextMs, open, now);
    if (gate != gui::WakeOpening) {
        wake.state = gate;
        (void)gui::phoneWakeWrite(gui::kPhoneWakePath, wake);
        return false;
    }
    if (display.fd < 0) {
        if (display.start(now)) {
            return false;
        }
        wake.state = gui::WakeBusy;
    } else {
        const int alone = display.poll(now);
        if (alone < 0) {
            return false;
        }
        if (alone) {
            request = wake;
            opening = true;
            openingMs = now;
            return true;
        }
        wake.state = gui::WakeBusy;
    }
    (void)gui::phoneWakeWrite(gui::kPhoneWakePath, wake);
    return false;
}
void PhoneRuntime::openFailed() {
    if (opening) {
        request.state = gui::WakeBusy;
        (void)gui::phoneWakeWrite(gui::kPhoneWakePath, request);
        opening = false;
    }
}
} // namespace plugin
} // namespace nbt
