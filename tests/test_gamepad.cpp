// SPDX-License-Identifier: MPL-2.0
#include "nbt/bridge.h"
#include "nbt/gamepad/input.hpp"
#include "nbt/gamepad/relay.hpp"
#include "nbt/gamepad/reports.hpp"
#include "nbt/gamepad/service.hpp"
#include "nbt/gamepad/shared_state.hpp"
#include "nbt/gui/gamepad_input.hpp"
#include "nbt/util.h"
#include <assert.h>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace nbt::gamepad;
static void wire() {
    nbt_gamepad_wire_state state = {}, decoded = {};
    state.connected = 1;
    state.sequence = UINT32_MAX;
    state.buttons = NBT_GAMEPAD_BUTTONS;
    state.lx = -32768;
    state.ly = 32767;
    state.rt = 65535;
    uint8_t bytes[25];
    assert(nbt_gamepad_wire_encode(&state, bytes, sizeof(bytes)) == 24);
    assert(!nbt_gamepad_wire_decode(bytes, 24, &decoded));
    assert(decoded.lx == -32768 && decoded.ly == 32767 && decoded.rt == 65535);
    for (unsigned n = 0; n < 24; ++n) {
        assert(nbt_gamepad_wire_decode(bytes, n, &decoded));
    }
    assert(nbt_gamepad_wire_decode(bytes, 25, &decoded));
    for (unsigned i = 0; i < 3; ++i) {
        uint8_t saved = bytes[i];
        bytes[i] = 2;
        assert(nbt_gamepad_wire_decode(bytes, 24, &decoded));
        bytes[i] = saved;
    }
    // Byte 3 is the family (bridge 1.5): 0-3 while connected, else rejected.
    assert(!decoded.family);
    for (unsigned family = 1; family <= NBT_GAMEPAD_FAMILY_MAX; ++family) {
        bytes[3] = static_cast<uint8_t>(family);
        assert(!nbt_gamepad_wire_decode(bytes, 24, &decoded) && decoded.family == family);
    }
    bytes[3] = NBT_GAMEPAD_FAMILY_MAX + 1;
    assert(nbt_gamepad_wire_decode(bytes, 24, &decoded));
    state.family = NBT_GAMEPAD_FAMILY_DUALSENSE;
    assert(nbt_gamepad_wire_encode(&state, bytes, 24) == 24 && bytes[3] == 3);
    state.family = NBT_GAMEPAD_FAMILY_MAX + 1;
    assert(nbt_gamepad_wire_encode(&state, bytes, 24) < 0);
    nbt_gamepad_wire_state none = {};
    none.family = NBT_GAMEPAD_FAMILY_XBOX; // a disconnected pad has no family
    assert(nbt_gamepad_wire_encode(&none, bytes, 24) < 0);
    state.family = 0;
    state.connected = 0;
    assert(nbt_gamepad_wire_encode(&state, bytes, 24) < 0);
    assert(nbt_gamepad_axis(-32768, 6553) == -32767);
    assert(nbt_gamepad_axis(32767, 6553) == 32767);
    assert(!nbt_gamepad_axis(-6553, 6553) && !nbt_gamepad_axis(6553, 6553));
    assert(nbt_gamepad_axis(6554, 6553) > 0 && !nbt_gamepad_axis(100, 32767));
}
static void reports() {
    assert(identify(0x045e, 0x02d1, 255, 0x47, 0xd0) == Gip);
    assert(identify(0x045e, 0x02ea, 255, 0x47, 0xd0) == Gip);
    assert(identify(0x045e, 0x0b12, 255, 0x47, 0xd0) == Gip);
    assert(identify(0x0e6f, 0x0165, 255, 0x47, 0xd0) == Gip);
    assert(identify(0x054c, 0x05c4, 3, 0, 0) == DualShock4);
    assert(identify(0x054c, 0x09cc, 3, 0, 0) == DualShock4);
    assert(identify(0x054c, 0x0ce6, 3, 0, 0) == DualSense);
    assert(identify(0x054c, 0x0df2, 3, 0, 0) == DualSense);
    assert(identify(0x054c, 0x0ba0, 3, 0, 0) == Unsupported);
    assert(identify(0x054c, 0x0ce6, 1, 0, 0) == Unsupported);
    for (unsigned f = DualShock4; f <= DualSense; ++f) {
        uint8_t p[64] = {1, 128, 128, 128, 128};
        const size_t b = f == DualShock4 ? 5 : 8, t = f == DualShock4 ? 8 : 5;
        p[b] = 8;
        p[t] = 255;
        nbt_gamepad_wire_state s = {};
        assert(decodeSony(static_cast<Family>(f), p, 64, s) == 1);
        assert(!s.buttons && !s.lx && !s.ly && s.lt == 65535);
        for (unsigned hat = 0; hat < 8; ++hat) {
            p[b] = static_cast<uint8_t>(hat | 32);
            assert(decodeSony(static_cast<Family>(f), p, 64, s) == 1);
            assert(s.buttons & NBT_GAMEPAD_SOUTH);
        }
        p[b] = 9;
        s.buttons = 123;
        assert(decodeSony(static_cast<Family>(f), p, 64, s) < 0 && s.buttons == 123);
        for (unsigned n = 0; n < 64; ++n) {
            assert(decodeSony(static_cast<Family>(f), p, n, s) < 0);
        }
    }
    GipSession gip;
    gip.reset(0x045e, 0x02ea);
    uint8_t out[64];
    unsigned count = 0;
    while (gip.output(out)) {
        ++count;
    }
    assert(count == 4);
    nbt_gamepad_wire_state s = {};
    uint8_t p[18] = {0x20, 0, 1, 14, 0x10, 0x10, 0xff, 3, 0xff, 3, 0, 0x80};
    assert(gip.feed(p, 18, s) == 1 && s.lx == -32768 && s.lt == 65535);
    assert(s.buttons == (NBT_GAMEPAD_SOUTH | NBT_GAMEPAD_LB));
    for (unsigned n = 0; n < 18; ++n) {
        assert(gip.feed(p, n, s) < 0);
    }
    p[7] = 4;
    assert(gip.feed(p, 18, s) < 0);
    p[7] = 3;
    const uint8_t guide[] = {7, 0x30, 0xab, 2, 1, 0x5b};
    assert(gip.feed(guide, sizeof(guide), s) == 1 && (s.buttons & NBT_GAMEPAD_GUIDE));
    assert(gip.output(out) == 13 && out[0] == 1 && out[2] == 0xab && out[5] == 7);
    uint8_t arrival[32] = {2, 0x30, 9, 28};
    assert(gip.feed(arrival, sizeof(arrival), s) == 1 && !s.connected && !s.buttons);
    assert(gip.output(out) == 13 && out[2] == 9);
    gip.reset(0x24c6, 0x543a);
    count = 0;
    while (gip.output(out)) {
        ++count;
    }
    assert(count == 5);
    for (unsigned i = 0; i < 40; ++i) {
        (void)gip.feed(guide, sizeof(guide), s);
    }
    count = 0;
    while (gip.output(out)) {
        ++count;
    }
    assert(count == 16);
}
static void input() {
    nbt_gamepad_wire_state s = {};
    s.connected = 1;
    s.sequence = UINT32_MAX;
    uint8_t p[24];
    Input in;
    s.buttons = NBT_GAMEPAD_SOUTH;
    s.family = NBT_GAMEPAD_FAMILY_DUALSHOCK4;
    assert(nbt_gamepad_wire_encode(&s, p, 24) > 0 && in.feed(p, 24, 100));
    assert(!in.state().buttons);                                // held across focus acquisition
    assert(in.state().family == NBT_GAMEPAD_FAMILY_DUALSHOCK4); // known before it is armed
    s.buttons = 0;
    s.sequence = 0;
    (void)nbt_gamepad_wire_encode(&s, p, 24);
    assert(in.feed(p, 24, 101));
    s.buttons = NBT_GAMEPAD_SOUTH;
    s.sequence = 1;
    (void)nbt_gamepad_wire_encode(&s, p, 24);
    assert(in.feed(p, 24, 102));
    assert(in.state().buttons == NBT_GAMEPAD_SOUTH && !in.feed(p, 24, 103));
    Navigation nav;
    s.buttons = NBT_GAMEPAD_UP;
    assert(nav.update(s, 0).pressed == NBT_GAMEPAD_UP);
    assert(!nav.update(s, 399).repeated);
    assert(nav.update(s, 400).repeated == NBT_GAMEPAD_UP);
    assert(!nav.update(s, 499).repeated);
    assert(nav.update(s, 500).repeated == NBT_GAMEPAD_UP);
    int a = -1, b = -1;
    assert(Service::socket(a, b));
    assert(send(a, p, 24, 0) == 24);
    in.poll(b, 603);
    assert(!in.state().connected);
    close(a);
    close(b);
}
/* io-usb refusing the connection, as the car might. */
class RefusedUsb : public UsbTransport {
public:
    bool start() override {
        return false;
    }
    void stop() override {
    }
    int poll(uint8_t *, size_t &) override {
        return -1;
    }
    bool write(const uint8_t *, size_t) override {
        return false;
    }
    Family family() const override {
        return Unsupported;
    }
    unsigned vendor() const override {
        return 0;
    }
    unsigned product() const override {
        return 0;
    }
    UsbProblem problem() const override {
        const UsbProblem refused = {19, 0, 0, NULL};
        return refused;
    }
};
class SimulatedUsb : public UsbTransport {
public:
    SimulatedUsb() : running_(false) {
    }
    bool start() override {
        running_ = true;
        return true;
    }
    void stop() override {
        running_ = false;
    }
    int poll(uint8_t p[64], size_t &n) override {
        memset(p, 0, 64);
        p[0] = 1;
        p[1] = p[2] = p[3] = p[4] = 128;
        p[5] = 8;
        n = 64;
        return running_ ? 1 : -1;
    }
    bool write(const uint8_t *p, size_t n) override {
        if (n <= sizeof(written)) {
            memcpy(written, p, n);
            writtenSize = n;
        }
        ++writes;
        return true;
    }
    Family family() const override {
        return DualShock4;
    }
    static uint8_t written[64];
    static size_t writtenSize;
    static unsigned writes;
    unsigned vendor() const override {
        return 0x054c;
    }
    unsigned product() const override {
        return 0x09cc;
    }

private:
    bool running_;
};
uint8_t SimulatedUsb::written[64];
size_t SimulatedUsb::writtenSize;
unsigned SimulatedUsb::writes;
static void service() {
    char root[] = "/tmp/nbt-gamepad-XXXXXX", path[256], error[128];
    assert(mkdtemp(root));
    snprintf(path, sizeof(path), "%s/state", root);
    assert(!mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/state/manager", root);
    assert(!mkdir(path, 0700));
    nbt_fs fs;
    assert(!nbt_fs_init(&fs, root, error, sizeof(error)));
    int a = -1, b = -1;
    assert(Service::socket(a, b));
    char shared[256];
    snprintf(shared, sizeof(shared), "%s/state/manager/pad-state", root);
    Service svc;
    assert(svc.start(&fs, a, std::unique_ptr<UsbTransport>(new (std::nothrow) SimulatedUsb()),
                     shared));
    bool connected = false;
    Input in;
    for (unsigned i = 0; i < 200 && !connected; ++i) {
        in.poll(b, i * 5u);
        connected = in.state().connected != 0;
        usleep(5000);
    }
    assert(connected);
    // The Status page's report: the open pad, held by this service.
    Service::Report report = svc.report();
    assert(report.running && report.owner && !report.lockRefused);
    assert(report.family == DualShock4 && report.vendor == 0x054c && report.product == 0x09cc);
    assert(!report.problem.connectError && !report.problem.step);
    // io-usb refusing the connection shows its error. (Record locks do not
    // conflict within one process: the lock held by another process is
    // test_gamepad.py's.)
    {
        int c = -1, d = -1;
        assert(Service::socket(c, d));
        Service other;
        assert(other.start(&fs, c, std::unique_ptr<UsbTransport>(new (std::nothrow) RefusedUsb()),
                           NULL));
        for (unsigned i = 0; i < 200 && !other.report().problem.connectError; ++i) {
            usleep(5000);
        }
        report = other.report();
        assert(report.running && !report.owner && !report.lockRefused);
        assert(report.family == Unsupported && report.problem.connectError == 19);
        other.stop();
        assert(!other.report().running);
        close(d);
    }
    // A DualShock 4 gets its light bar set once, blue (report 0x05, 32 bytes).
    assert(SimulatedUsb::writes == 1 && SimulatedUsb::writtenSize == 32);
    assert(SimulatedUsb::written[0] == 0x05 && SimulatedUsb::written[1] == 0x07);
    assert(!SimulatedUsb::written[6] && !SimulatedUsb::written[7] && SimulatedUsb::written[8]);
    // The same state is published for the application hosts, and goes stale
    // once the GUI stops.
    StateReader shared_reader;
    uint64_t now = 0;
    assert(!nbt_monotonic_ms(&now) && shared_reader.read(shared, now).connected);
    svc.stop();
    close(b);
    assert(!svc.report().running && svc.report().family == Unsupported);
    assert(!shared_reader.read(shared, now + kStateStaleMs + 1).connected);
    assert(!unlink(shared));
    snprintf(path, sizeof(path), "%s/state/manager/gamepad.lock", root);
    assert(!unlink(path));
    snprintf(path, sizeof(path), "%s/state/manager", root);
    assert(!rmdir(path));
    snprintf(path, sizeof(path), "%s/state", root);
    assert(!rmdir(path));
    assert(!rmdir(root));
}
static void records() {
    nbt_gamepad_wire_state s = {};
    s.connected = 1;
    s.buttons = NBT_GAMEPAD_SOUTH;
    s.lx = -100;
    uint8_t r[kStateRecordSize];
    encodeRecord(s, 7, 1000, r);
    nbt_gamepad_wire_state d = decodeRecord(r, sizeof(r), 1200);
    assert(d.connected && d.buttons == NBT_GAMEPAD_SOUTH && d.lx == -100);
    assert(!decodeRecord(r, sizeof(r), 1000 + kStateStaleMs + 1).connected); // stale
    assert(!decodeRecord(r, sizeof(r), 999).connected);                      // from the future
    assert(!decodeRecord(r, sizeof(r) - 1, 1200).connected);                 // short
    r[40] ^= 1;                                                              // torn
    assert(!decodeRecord(r, sizeof(r), 1200).connected);
    r[40] ^= 1;
    r[0] = 'X'; // not ours
    assert(!decodeRecord(r, sizeof(r), 1200).connected);
    s.buttons = 1u << 20; // invalid: published as nothing
    encodeRecord(s, 8, 1000, r);
    assert(!decodeRecord(r, sizeof(r), 1200).connected);
    // The family rides in byte 44; the wire state's byte 3 stays 0 for hosts
    // of the previous release, which read the record and reject it there.
    s.buttons = 0;
    s.family = NBT_GAMEPAD_FAMILY_XBOX;
    encodeRecord(s, 9, 1000, r);
    assert(r[16 + 3] == 0 && r[44] == NBT_GAMEPAD_FAMILY_XBOX);
    nbt_gamepad_wire_state old;
    assert(!nbt_gamepad_wire_decode(r + 16, NBT_GAMEPAD_WIRE_SIZE, &old) && !old.family);
    d = decodeRecord(r, sizeof(r), 1200);
    assert(d.connected && d.family == NBT_GAMEPAD_FAMILY_XBOX);
    r[44] = NBT_GAMEPAD_FAMILY_MAX + 1;
    assert(!decodeRecord(r, sizeof(r), 1200).connected);
    r[44] = 0;
    r[45] = 1;
    assert(!decodeRecord(r, sizeof(r), 1200).connected);
    s.connected = 0;
    encodeRecord(s, 10, 1000, r); // a family without a pad: published as nothing
    assert(!decodeRecord(r, sizeof(r), 1200).connected && !r[44]);
}
static void guiInput() {
    int host = -1, child = -1;
    assert(Service::socket(host, child));
    nbt::gui::GamepadInput input;
    input.attachForTesting(child);
    nbt::gui::UiController ui;
    ui.go(nbt::gui::Apps);
    ui.appCount = 1;
    strcpy(ui.apps[0], "test");
    ui.queued = ui.outstanding = 0;
    ui.clockMs = 100;
    nbt_gamepad_wire_state s = {};
    s.connected = 1;
    uint8_t packet[24];
    const auto sendState = [&]() {
        ++s.sequence;
        assert(nbt_gamepad_wire_encode(&s, packet, sizeof(packet)) == 24);
        assert(send(host, packet, sizeof(packet), 0) == 24);
    };
    sendState();
    s.buttons = NBT_GAMEPAD_SOUTH;
    sendState();
    s.connected = 0;
    s.buttons = 0;
    sendState();
    (void)input.poll(ui, 100, true);
    assert(!ui.holding && ui.queued == 0); // disconnect cancels, never starts
    s.connected = 1;
    sendState();
    s.buttons = NBT_GAMEPAD_SOUTH;
    sendState();
    s.buttons = 0;
    sendState();
    (void)input.poll(ui, 110, true);
    assert(ui.queued == nbt::gui::R_START); // short tap survives a single receive batch
    // On the gamepad test the pad's buttons are shown, never acted on: B and
    // the D-pad do not leave, A selects nothing, Menu does not go home.
    ui.queued = 0;
    ui.go(nbt::gui::GamepadTest);
    for (const uint32_t button :
         {NBT_GAMEPAD_EAST, NBT_GAMEPAD_LEFT, NBT_GAMEPAD_SOUTH, NBT_GAMEPAD_MENU, 0u}) {
        s.buttons = button;
        sendState();
    }
    assert(input.poll(ui, 120, true)); // pressing counts as activity
    assert(ui.screen == nbt::gui::GamepadTest && !ui.queued && !ui.holding);
    assert(input.state().connected && !input.state().buttons);
    close(host);
}
static int serve(const char *root, const char *descriptor, const char *shared) {
    char error[128], *end = NULL;
    const long fd = strtol(descriptor, &end, 10);
    nbt_fs fs;
    if (!end || *end || fd < 3 || fd > 1024 || nbt_fs_init(&fs, root, error, sizeof(error))) {
        return 2;
    }
    Service svc;
    if (!svc.start(&fs, static_cast<int>(fd),
                   std::unique_ptr<UsbTransport>(new (std::nothrow) SimulatedUsb()), shared)) {
        return 1;
    }
    char command;
    while (read(0, &command, 1) == 1 && command != 'q') {
        if (command == 'r') { // the Status page's report: running, owner, lock refused
            const Service::Report r = svc.report();
            printf("report %d %d %d\n", r.running, r.owner, r.lockRefused);
            fflush(stdout);
        }
    }
    svc.stop();
    return 0;
}
/* An application host's relay of the owner's published pads (test_gamepad.py),
 * for an application of bridge 1.5 (`family` 1) or older (0). */
static int relay(const char *descriptor, const char *shared, const char *family) {
    char *end = NULL;
    const long fd = strtol(descriptor, &end, 10);
    if (!end || *end || fd < 3 || fd > 1024) {
        return 2;
    }
    Relay relay;
    if (!relay.start(static_cast<int>(fd), shared, family && !strcmp(family, "1"))) {
        return 1;
    }
    relay.setActive(true);
    char command;
    while (read(0, &command, 1) == 1 && command != 'q') {
        relay.setActive(command != 'p'); // 'p': paused
    }
    relay.stop();
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "--serve")) {
        return serve(argv[2], argv[3], argv[4]);
    }
    if ((argc == 4 || argc == 5) && !strcmp(argv[1], "--relay")) {
        return relay(argv[2], argv[3], argc == 5 ? argv[4] : NULL);
    }
    wire();
    reports();
    input();
    service();
    records();
    guiInput();
    puts("gamepad tests: OK");
    return 0;
}
