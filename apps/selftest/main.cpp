// SPDX-License-Identifier: MPL-2.0
// NBT Manager's self test application (docs/self-test.md), bundled with each
// release in three forms (apps/selftest/selftest.mk): a native program, a
// guest and a module. Each checks what the manager gives it and writes its
// report for the manager; the module (NBT_SELFTEST_INTERACTIVE) also plays a
// rising tone, asks for a turn and a push of the controller, and publishes a
// phone page. All of them show colour bars, which the driver is asked about.
#include "probes.hpp"
#include "report.hpp"

#include "nbt/app/audio_pcm.hpp"
#include "nbt/app/clock.hpp"
#include "nbt/app/companion.hpp"
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

#ifndef NBT_SELFTEST_FORM
#define NBT_SELFTEST_FORM "native"
#endif
#ifndef NBT_SELFTEST_CAPS
#define NBT_SELFTEST_CAPS "show,status"
#endif
#ifndef NBT_SELFTEST_INTERACTIVE
#define NBT_SELFTEST_INTERACTIVE 0
#endif

namespace {

using nbt::frontend::Overlay;
using selftest::Report;

const uint64_t kBarsMs = 4000, kAskMs = 15000, kDoneMs = 1500;
const uint32_t kWhite = 0xffffffffu, kGrey = 0xb0b8c0ffu;

// One rising tone, 400 to 1600 Hz in 3 s, from half a second in: a break or a
// skip in the sound is heard as a click or a jump in it.
struct Sweep {
    unsigned rate;
    uint64_t sample;
    uint32_t phase; // of the wave, a full turn = 2^32
};

void sweep(void *context, int16_t *out, int frames) {
    Sweep &s = *static_cast<Sweep *>(context);
    const uint64_t start = s.rate / 2, length = 3ull * s.rate, fade = s.rate / 50;
    for (int i = 0; i < frames; ++i, ++s.sample) {
        int value = 0;
        if (s.sample >= start && s.sample < start + length) {
            const uint64_t at = s.sample - start;
            const uint64_t hz = 400 + 1200 * at / length;
            s.phase += static_cast<uint32_t>((hz << 32) / s.rate);
            const int turn = static_cast<int>(s.phase >> 16);            // 0..65535
            const int wave = turn < 32768 ? turn - 16384 : 49152 - turn; // a triangle
            const uint64_t edge =
                at < length - 1 - at ? at : length - 1 - at; // no click at the ends
            const int gain = edge < fade ? static_cast<int>(edge * 256 / fade) : 256;
            value = wave * 3000 / 16384 * gain / 256;
        }
        out[2 * i] = out[2 * i + 1] = static_cast<int16_t>(value);
    }
}

enum Phase { kBars, kTurn, kPush, kFinish, kDone };

struct Controls {
    bool turned, pushed, back;
};

void onController(void *context, const nbt_controller_wire_state &,
                  const nbt::controller::Event &event) {
    Controls &c = *static_cast<Controls *>(context);
    c.turned = c.turned || event.rotation != 0;
    c.pushed = c.pushed || (event.pressed & NBT_CONTROLLER_PUSH);
    c.back = c.back || (event.pressed & NBT_CONTROLLER_BACK);
}

void centred(Overlay &o, int width, int top, int scale, const char *text, uint32_t rgba) {
    o.text((width - Overlay::text_width(text, scale)) / 2, top, scale, text, rgba);
}

void draw(nbt::app::VideoEgl &video, Overlay &overlay, Phase phase, const Report &report) {
    const int w = video.window_width(), h = video.window_height();
    const int scale = Overlay::scale_for(h), glyph = Overlay::kGlyph;
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    overlay.begin(w, h);
    const int top = h / 5, bar = h * 3 / 5;
    overlay.box(0, top, w / 3, bar, 0xd02020ffu);
    overlay.box(w / 3, top, w - 2 * (w / 3), bar, 0x20b040ffu);
    overlay.box(w - w / 3, top, w / 3, bar, 0x2050e0ffu);
    centred(overlay, w, glyph * scale / 2, 2 * scale, "NBT MANAGER SELF TEST", kWhite);
    char line[64];
    if (phase == kTurn) {
        snprintf(line, sizeof(line), "TURN THE CONTROLLER");
    } else if (phase == kPush) {
        snprintf(line, sizeof(line), "PUSH THE CONTROLLER");
    } else if (phase == kDone) {
        snprintf(line, sizeof(line), "%s: %u OK, %u FAILED", NBT_SELFTEST_FORM,
                 static_cast<unsigned>(report.count(selftest::kPass)),
                 static_cast<unsigned>(report.count(selftest::kFail)));
    } else {
        snprintf(line, sizeof(line), "%s", NBT_SELFTEST_FORM);
    }
    for (char *c = line; *c; ++c) {
        *c = static_cast<char>(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
    }
    centred(overlay, w, top + bar + glyph * scale / 2, 2 * scale, line,
            phase == kTurn || phase == kPush ? kWhite : kGrey);
    overlay.end();
    video.swap();
}

void showPage(nbt::app::Companion &page, Phase phase, unsigned percent) {
    page.begin();
    page.title("NBT Manager self test");
    page.text(phase == kTurn   ? "Turn the iDrive controller."
              : phase == kPush ? "Push the iDrive controller."
                               : "Checking...");
    page.progress("Self test", percent);
}

int idle(nbt::app::Lifecycle &lifecycle) { // a health check: nothing to test
    if (!lifecycle.send_ready()) {
        return 1;
    }
    while (!lifecycle.stopping()) {
        lifecycle.tick(nbt::app::monotonic_ms(), "Self test", NULL);
        usleep(200000);
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    nbt::app::log_init("selftest");
    nbt::app::Lifecycle lifecycle;
    const int started = lifecycle.init(argc, argv);
    if (started != 0) {
        return started;
    }
    nbt::app::log_install_crash_handlers();
    if (!lifecycle.is_user_mode()) {
        return idle(lifecycle);
    }
    nbt::app::log_info("self test (%s), caps %s", NBT_SELFTEST_FORM, lifecycle.bridge_caps());
    Report report(NBT_SELFTEST_FORM);
    selftest::checkBridge(lifecycle, NBT_SELFTEST_CAPS, report);
    nbt::app::VideoEgl video;
    Overlay overlay;
    const nbt::app::VideoConfig window = {"NBT Manager self test", 1280, 480};
    if (!video.init(lifecycle.surface_id(), &window) || !overlay.init()) {
        report.set("screen", selftest::kFail, "no window");
    }
    if (!lifecycle.send_ready()) {
        return 1;
    }
    report.set("status", lifecycle.status_fd() >= 0 ? selftest::kPass : selftest::kFail,
               lifecycle.status_fd() >= 0 ? "written" : "not granted");
    selftest::checkFiles(lifecycle, report);
    uint64_t now = nbt::app::monotonic_ms();
    selftest::CarDataProbe carData;
    carData.start(now, report);
    selftest::DiagProbe diag;
    diag.start(report);
    nbt::app::AudioPcm audio;
    Sweep tone = {static_cast<unsigned>(nbt::app::kAudioDefaultRate), 0, 0};
    if (audio.open(nbt::app::kAudioDefaultRate, &sweep, &tone, NBT_SELFTEST_INTERACTIVE != 0)) {
        tone.rate = static_cast<unsigned>(audio.sample_rate());
        report.set("sound", selftest::kPass, "%s %d Hz",
                   getenv("NBT_AUDIO_MIXER") ? "mixer" : "device", audio.sample_rate());
    } else {
        report.set("sound", selftest::kFail, "no audio output");
    }
    nbt::app::Companion page;
    const bool paged = NBT_SELFTEST_INTERACTIVE && page.init();
    if (NBT_SELFTEST_INTERACTIVE && !paged) {
        report.set("phone", selftest::kFail, "not granted");
    }
    nbt::controller::Input input;
    Controls controls = {false, false, false};
    const uint64_t start = now;
    uint64_t phaseAt = start, framesAt = 0;
    unsigned frames = 0;
    Phase phase = kBars;
    while (phase != kDone || now - phaseAt < kDoneMs) {
        if (lifecycle.stopping()) {
            if (NBT_SELFTEST_INTERACTIVE && !report.has("controller")) {
                report.set("controller", selftest::kSkip, "stopped");
            }
            break;
        }
        now = nbt::app::monotonic_ms();
        lifecycle.tick(now, "Self test", NULL);
        (void)lifecycle.consume_show_requested(); // every frame is drawn again anyway
        if (lifecycle.controller_fd() >= 0) {
            input.poll(lifecycle.controller_fd(), &onController, &controls);
        }
        carData.poll(now, report);
        const bool diagDone = diag.poll(report);
        if (paged) {
            showPage(page, phase, static_cast<unsigned>(phase) * 25u);
            if (!report.has("phone")) {
                report.set("phone", page.publish() ? selftest::kPass : selftest::kFail,
                           "published");
            } else {
                (void)page.publish();
            }
        }
        if (phase == kBars && now - phaseAt >= kBarsMs) {
            framesAt = now - start;
            phase = NBT_SELFTEST_INTERACTIVE ? kTurn : kFinish;
            phaseAt = now;
        } else if ((phase == kTurn || phase == kPush) && controls.back) {
            report.set("controller", selftest::kSkip, "BACK pushed");
            phase = kFinish;
        } else if (phase == kTurn && (controls.turned || now - phaseAt >= kAskMs)) {
            phase = controls.turned ? kPush : kFinish;
            phaseAt = now;
            if (!controls.turned) {
                report.set("controller", selftest::kFail, "no turn in 15 s");
            }
        } else if (phase == kPush && (controls.pushed || now - phaseAt >= kAskMs)) {
            report.set("controller", controls.pushed ? selftest::kPass : selftest::kFail,
                       controls.pushed ? "turned and pushed" : "no push in 15 s");
            phase = kFinish;
        } else if (phase == kFinish && carData.done() && diagDone) {
            phase = kDone;
            phaseAt = now;
            if (!report.has("screen")) {
                const unsigned fps =
                    framesAt ? static_cast<unsigned>(frames * 1000u / framesAt) : 0;
                report.set("screen", fps ? selftest::kPass : selftest::kFail, "%u fps", fps);
            }
            if (!report.write(lifecycle.data_dir())) {
                nbt::app::log_info("cannot write the report in %s", lifecycle.data_dir());
            }
        }
        if (video.ready()) {
            draw(video, overlay, phase, report);
            frames += phase == kBars ? 1 : 0;
        }
        usleep(16000);
    }
    if (phase != kDone && !report.write(lifecycle.data_dir())) {
        nbt::app::log_info("cannot write the report in %s", lifecycle.data_dir());
    }
    nbt::app::log_info("self test (%s): %u passed, %u failed, %u skipped", NBT_SELFTEST_FORM,
                       static_cast<unsigned>(report.count(selftest::kPass)),
                       static_cast<unsigned>(report.count(selftest::kFail)),
                       static_cast<unsigned>(report.count(selftest::kSkip)));
    audio.close();
    overlay.shutdown();
    video.shutdown();
    lifecycle.close_fds();
    return 0;
}
