// SPDX-License-Identifier: MPL-2.0
// The kits' test application (tools/build_windows_kit.py) on the NBT App SDK,
// built without the QNX SDP (apps/test/test-app.mk) in two forms: an ordinary
// QNX 6.5 program, and a module that the release's nbt-runner runs (bridge
// 1.7). It shows HELLO WORLD on its Background color, its version and form;
// publishes "Hello <version>" as its status; follows live settings; and ends
// on BACK or MENU while its Use the controller setting is on. v3 fails 5 s after it
// starts, so its health check fails. Its Crash on purpose setting, changed from the
// phone while it runs, makes it crash for a crash report (PROTOCOL.md, "Crash
// reports"): `fault` writes to an unmapped address (SIGSEGV, as a real bug would),
// `hang` stops its heartbeat. The host tests use tests/fake_app.cpp.
#include "nbt/app/audio_pcm.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/lifecycle.hpp"
#include "nbt/app/log.hpp"
#include "nbt/app/video.hpp"
#include "nbt/controller/input.hpp"
#include "nbt/frontend/overlay.hpp"

#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef NBT_TEST_VERSION
#define NBT_TEST_VERSION "v1"
#endif
#ifndef NBT_TEST_FORM
#define NBT_TEST_FORM "native"
#endif

namespace {

using nbt::frontend::Overlay;

struct Colour {
    const char *name;
    float red, green, blue;
};
// The manager GUI's own blue, red and green (src/gui/frame.cpp).
const Colour kColours[] = {{"blue", 0x1d / 255.0f, 0x4f / 255.0f, 0x91 / 255.0f},
                           {"red", 0x9a / 255.0f, 0x23 / 255.0f, 0x23 / 255.0f},
                           {"green", 0x1f / 255.0f, 0x7a / 255.0f, 0x3e / 255.0f},
                           {"black", 0.0f, 0.0f, 0.0f}};
const uint32_t kWhite = 0xffffffffu, kGrey = 0xd0d8e0ffu;

// test-service's sound, to hear NBT Manager's audio mixer play both at once
// (docs/audio-mixer.md): a short triangle-wave beep at `hz` every `period_ms`.
struct Beep {
    unsigned hz, period_ms, rate;
    uint64_t sample;
};

void beep(void *context, int16_t *out, int frames) {
    Beep &b = *static_cast<Beep *>(context);
    const uint64_t period = static_cast<uint64_t>(b.rate) * b.period_ms / 1000;
    const uint64_t length = b.rate / 5; // 200 ms
    for (int i = 0; i < frames; ++i, ++b.sample) {
        int value = 0;
        if (period && b.sample % period < length) {
            const unsigned step = static_cast<unsigned>(b.sample * b.hz % b.rate); // 0..rate-1
            const int ramp = static_cast<int>(step * 4000u / b.rate);              // 0..3999
            value = ramp < 2000 ? ramp * 2 - 2000 : 6000 - ramp * 2;               // -2000..2000
        }
        out[2 * i] = out[2 * i + 1] = static_cast<int16_t>(value);
    }
}

const Colour &background(const nbt::app::Lifecycle &lifecycle) {
    const char *name = lifecycle.settings().get("background", "blue");
    for (size_t i = 0; i < sizeof(kColours) / sizeof(kColours[0]); ++i) {
        if (!strcmp(name, kColours[i].name)) {
            return kColours[i];
        }
    }
    return kColours[0];
}

// The controller reaches an application granted `controller` while it is on
// the panel; the setting decides whether BACK or MENU ends it.
bool keysEnd(const nbt::app::Lifecycle &lifecycle) {
    return lifecycle.controller_fd() >= 0 &&
           strcmp(lifecycle.settings().get("controller", "on"), "off") != 0;
}

void onController(void *context, const nbt_controller_wire_state &,
                  const nbt::controller::Event &event) {
    if (event.pressed & (NBT_CONTROLLER_BACK | NBT_CONTROLLER_MENU)) {
        *static_cast<bool *>(context) = true;
    }
}

/* Crash on purpose: never returns when the setting asks for a crash. */
void crashIfAsked(const nbt::app::Lifecycle &lifecycle) {
    const char *crash = lifecycle.settings().get("crash", "off");
    if (!strcmp(crash, "fault")) {
        nbt::app::log_stage("crash on purpose");
        nbt::app::log_info("Crash on purpose: writing to address 0x10");
        *reinterpret_cast<volatile int *>(static_cast<uintptr_t>(0x10)) = 1;
    } else if (!strcmp(crash, "hang")) {
        nbt::app::log_info("Crash on purpose: no more heartbeats");
        for (;;) {
            sleep(1);
        }
    }
}

void centred(Overlay &overlay, int width, int top, int scale, const char *text, uint32_t rgba) {
    overlay.text((width - Overlay::text_width(text, scale)) / 2, top, scale, text, rgba);
}

void draw(nbt::app::VideoEgl &video, Overlay &overlay, const nbt::app::Lifecycle &lifecycle) {
    const int width = video.window_width(), height = video.window_height();
    const Colour &colour = background(lifecycle);
    glViewport(0, 0, width, height);
    glClearColor(colour.red, colour.green, colour.blue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // "HELLO WORLD" across most of the width, as the manager's own test frame.
    int scale = width / (11 * Overlay::kGlyph + 2 * Overlay::kGlyph);
    if (scale > height / (2 * Overlay::kGlyph)) {
        scale = height / (2 * Overlay::kGlyph);
    }
    if (scale < 3) {
        scale = 3;
    }
    const int small = scale / 3, glyph = Overlay::kGlyph;
    overlay.begin(width, height);
    centred(overlay, width, height / 2 - glyph * scale, scale, "HELLO WORLD", kWhite);
    centred(overlay, width, height / 2 + glyph * scale / 2, 2 * small,
            "TEST " NBT_TEST_VERSION " - " NBT_TEST_FORM, kGrey);
    if (keysEnd(lifecycle)) {
        centred(overlay, width, height - 3 * glyph * small, small, "BACK TO EXIT", kGrey);
    }
    overlay.end();
    video.swap();
}

} // namespace

int main(int argc, char **argv) {
    nbt::app::log_init("test");
    nbt::app::Lifecycle lifecycle;
    const int started = lifecycle.init(argc, argv);
    if (started != 0) {
        return started;
    }
    nbt::app::log_install_crash_handlers();
    nbt::app::log_info("test %s (%s), caps %s", NBT_TEST_VERSION, NBT_TEST_FORM,
                       lifecycle.bridge_caps());
    nbt::app::VideoEgl video;
    Overlay overlay;
    // Its service (bridge 1.9, test-service in the kits) runs without a window
    // and notes that it still runs; a health check runs without a window too.
    const char *role = getenv("NBT_ROLE");
    const bool service = role && !strcmp(role, "service");
    // With a role (test-service): the application beeps at 660 Hz every 2 s,
    // its service at 440 Hz every 4 s.
    nbt::app::AudioPcm audio;
    Beep tone = {service ? 440u : 660u, service ? 4000u : 2000u, 0, 0};
    if (role && lifecycle.is_user_mode()) {
        tone.rate = static_cast<unsigned>(nbt::app::kAudioDefaultRate);
        if (audio.open(nbt::app::kAudioDefaultRate, &beep, &tone, true)) {
            tone.rate = static_cast<unsigned>(audio.sample_rate());
            nbt::app::log_info("beeping at %u Hz every %u s (%d Hz output)", tone.hz,
                               tone.period_ms / 1000, audio.sample_rate());
        }
    }
    if (lifecycle.is_user_mode() && !service) {
        const nbt::app::VideoConfig window = {"NBT Manager test " NBT_TEST_VERSION, 800, 480};
        if (!video.init(lifecycle.surface_id(), &window) || !overlay.init()) {
            nbt::app::log_info("no window: the car will not show it");
            return 1;
        }
    }
    if (!lifecycle.send_ready()) {
        return 1;
    }
    nbt::controller::Input controller;
    const uint64_t start = nbt::app::monotonic_ms();
    uint64_t noted = start;
    unsigned notes = 0;
    int result = 0;
    while (!lifecycle.stopping()) {
        const uint64_t now = nbt::app::monotonic_ms();
#ifdef NBT_TEST_FAIL_AFTER
        if (now - start >= NBT_TEST_FAIL_AFTER * 1000u) {
            nbt::app::log_info("failing on purpose after %d s", NBT_TEST_FAIL_AFTER);
            result = 1;
            break;
        }
#else
        (void)start;
#endif
        if (service && now - noted >= 30000) {
            noted = now;
            nbt::app::log_info("service still running (%u)", ++notes);
        }
        bool leave = false;
        if (lifecycle.controller_fd() >= 0) {
            controller.poll(lifecycle.controller_fd(), &onController, &leave);
        }
        if (leave && keysEnd(lifecycle)) {
            nbt::app::log_info("BACK or MENU: ending");
            break;
        }
        bool changed = false;
        lifecycle.tick(now, "Hello " NBT_TEST_VERSION, &changed);
        if (changed) {
            nbt::app::log_info("settings changed: background %s", background(lifecycle).name);
        }
        if (lifecycle.is_user_mode()) { // never in a health check
            crashIfAsked(lifecycle);
        }
        (void)lifecycle.consume_show_requested(); // every frame is drawn again anyway
        if (video.ready()) {
            draw(video, overlay, lifecycle);
        }
        usleep(50000);
    }
    audio.close();
    overlay.shutdown();
    video.shutdown();
    lifecycle.close_fds();
    return result;
}
