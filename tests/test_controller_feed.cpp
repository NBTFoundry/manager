// SPDX-License-Identifier: MPL-2.0
// Capability `controller`: the host's decoder (MOST messages to one state),
// the wire format and the SDK reader, end to end over a datagram socket pair.
// Messages follow NBT-AAD's car recordings (controls_2026-10-22_e5.txt and
// controls_2026-10-23_push.txt); the favourite keys' layout is HmiMain3D's
// CPxyCECenterStack decoder (docs/controller-input.md).
#include "nbt/controller/input.hpp"
#include "nbt/most/controller_feed.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

namespace {

using nbt::controller::Event;
using nbt::controller::Input;
using nbt::most::ControllerFeed;

struct Seen {
    unsigned count;
    Event total;
    uint32_t keys;
};

void collect(void *context, const nbt_controller_wire_state &state, const Event &event) {
    Seen *seen = static_cast<Seen *>(context);
    ++seen->count;
    seen->total.pressed |= event.pressed;
    seen->total.released |= event.released;
    seen->total.rotation += event.rotation;
    seen->keys = state.keys;
}

Seen drain(Input &input, int fd) {
    Seen seen;
    memset(&seen, 0, sizeof(seen));
    input.poll(fd, &collect, &seen);
    return seen;
}

void send(ControllerFeed &feed, unsigned fblock, unsigned inst, unsigned fkt,
          const unsigned char *data, size_t length) {
    nbt_most_message m;
    memset(&m, 0, sizeof(m));
    m.fblock = static_cast<uint8_t>(fblock);
    m.inst = static_cast<uint8_t>(inst);
    m.fkt = static_cast<uint16_t>(fkt);
    m.op = 0x0c;
    m.data = data;
    m.length = length;
    feed.message(m);
}

void knob(ControllerFeed &feed, unsigned position, unsigned push, unsigned tilt) {
    const unsigned char d[8] = {0x4c,
                                static_cast<unsigned char>(position >> 8),
                                static_cast<unsigned char>(position),
                                static_cast<unsigned char>(push),
                                static_cast<unsigned char>(tilt),
                                0,
                                0,
                                0xff};
    send(feed, 0xe5, 0x03, 0xc46, d, sizeof(d));
}

void slots(ControllerFeed &feed, unsigned slot, bool down) {
    unsigned char d[7] = {0, 0, 0, 0, 0, 0, 0};
    d[slot] = down ? 1 : 0;
    send(feed, 0xe5, 0x03, 0xc47, d, sizeof(d));
}

void wire() {
    nbt_controller_wire_state s, back;
    memset(&s, 0, sizeof(s));
    s.sequence = 7;
    s.keys = NBT_CONTROLLER_OPTION | NBT_CONTROLLER_FAVOURITE(7);
    s.rotation = -5;
    s.connected = 1;
    uint8_t out[NBT_CONTROLLER_WIRE_SIZE];
    CHECK(nbt_controller_wire_encode(&s, out, sizeof(out)) == NBT_CONTROLLER_WIRE_SIZE);
    CHECK(!nbt_controller_wire_decode(out, sizeof(out), &back));
    CHECK(back.sequence == 7 && back.keys == s.keys && back.rotation == -5 && back.connected);
    CHECK(nbt_controller_wire_decode(out, sizeof(out) - 1, &back)); // size
    out[3] = 1;
    CHECK(nbt_controller_wire_decode(out, sizeof(out), &back)); // reserved
    s.connected = 0;
    CHECK(nbt_controller_wire_encode(&s, out, sizeof(out)) < 0); // keys without a link
    s.keys = 1u << 12;
    s.connected = 1;
    CHECK(nbt_controller_wire_encode(&s, out, sizeof(out)) < 0); // undefined key
}

void controls() {
    ControllerFeed feed;
    uint16_t keys[16] = {0xe503, 0x2001};
    CHECK(feed.controls(keys, 2, 16) == 2); // not started: nothing added
    int fds[2];
    CHECK(!socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));
    feed.start(fds[0], NULL);
    const size_t n = feed.controls(keys, 2, 16);
    // Default E5 instances 01 02 03 05 10 12 14, 03 already there, and 15.0C.
    CHECK(n == 9 && keys[1] == 0x2001 && keys[2] == 0xe501 && keys[8] == 0x150c);
    CHECK(feed.controls(keys, 2, 4) == 4); // bounded
    close(fds[0]);
    close(fds[1]);
}

void feedAndRead() {
    int fds[2];
    CHECK(!socketpair(AF_UNIX, SOCK_DGRAM, 0, fds));
    CHECK(fcntl(fds[0], F_SETFL, O_NONBLOCK) != -1 && fcntl(fds[1], F_SETFL, O_NONBLOCK) != -1);
    ControllerFeed feed;
    Input input;
    feed.start(fds[0], NULL);
    feed.link(true);
    knob(feed, 0x8001, 0, 0);
    CHECK(drain(input, fds[1]).count == 0); // no surface yet: nothing sent

    // Shown while the push that opened the application is still held: no press.
    knob(feed, 0x8001, 1, 0);
    feed.show(true);
    Seen s = drain(input, fds[1]);
    CHECK(s.total.pressed == 0 && input.state().keys == 0);
    knob(feed, 0x8001, 0, 0);
    knob(feed, 0x8001, 1, 0);
    s = drain(input, fds[1]);
    CHECK(s.total.pressed == NBT_CONTROLLER_PUSH && s.keys == NBT_CONTROLLER_PUSH);
    knob(feed, 0x8001, 0, 0);
    CHECK(drain(input, fds[1]).total.released == NBT_CONTROLLER_PUSH);

    // Seven detents clockwise across the counter's centre, then two back.
    for (unsigned p = 0x8002; p <= 0x8008; ++p) {
        knob(feed, p, 0, 0);
    }
    knob(feed, 0x8006, 0, 0);
    CHECK(drain(input, fds[1]).total.rotation == 5);
    knob(feed, 0x7f00, 0, 0); // a re-base (262 detents): not input
    knob(feed, 0x7f01, 0, 0);
    CHECK(drain(input, fds[1]).total.rotation == 1);

    // Tilts on the 12-step clock, diagonals as two directions.
    knob(feed, 0x7ffe, 0, 1);
    CHECK(drain(input, fds[1]).keys == NBT_CONTROLLER_TILT_UP);
    knob(feed, 0x7ffe, 0, 8);
    CHECK(drain(input, fds[1]).keys == (NBT_CONTROLLER_TILT_DOWN | NBT_CONTROLLER_TILT_LEFT));
    knob(feed, 0x7ffe, 0, 0);
    (void)drain(input, fds[1]);

    // Hard keys by slot: 0 MENU, 1 BACK, 2 OPTION, 3 COM, 4 MEDIA, 5 NAV, 6 MAP.
    const uint32_t bySlot[] = {NBT_CONTROLLER_MENU, NBT_CONTROLLER_BACK,  NBT_CONTROLLER_OPTION,
                               NBT_CONTROLLER_COM,  NBT_CONTROLLER_MEDIA, NBT_CONTROLLER_NAV,
                               NBT_CONTROLLER_MAP};
    for (unsigned i = 0; i < 7; ++i) {
        slots(feed, i, true);
        CHECK(drain(input, fds[1]).total.pressed == bySlot[i]);
        slots(feed, i, false);
        CHECK(drain(input, fds[1]).total.released == bySlot[i]);
    }

    // Favourite key 3 (centre stack 0x15.0C, function 0xC02, one byte per key).
    unsigned char fav[8] = {0, 0, 1, 0, 0, 0, 0, 0};
    send(feed, 0x15, 0x0c, 0xc02, fav, sizeof(fav));
    CHECK(drain(input, fds[1]).total.pressed == NBT_CONTROLLER_FAVOURITE(2));
    // The volume button next to them (0xC01) is not the application's.
    send(feed, 0x15, 0x0c, 0xc01, fav, sizeof(fav));
    CHECK(drain(input, fds[1]).count == 0);
    fav[2] = 0;
    send(feed, 0x15, 0x0c, 0xc02, fav, sizeof(fav));
    CHECK(drain(input, fds[1]).total.released == NBT_CONTROLLER_FAVOURITE(2));

    // Paused with BACK held: released at once, quiet while paused, and the
    // still-held BACK is no press after resuming.
    slots(feed, 1, true);
    CHECK(drain(input, fds[1]).total.pressed == NBT_CONTROLLER_BACK);
    feed.pause(true);
    CHECK(drain(input, fds[1]).total.released == NBT_CONTROLLER_BACK);
    knob(feed, 0x7ff0, 0, 0);
    CHECK(drain(input, fds[1]).count == 0);
    feed.pause(false);
    s = drain(input, fds[1]);
    CHECK(s.total.pressed == 0 && s.total.rotation == 0);
    slots(feed, 1, false);
    knob(feed, 0x7ff1, 0, 0);
    CHECK(drain(input, fds[1]).total.rotation == 1);

    // A lost link releases everything.
    slots(feed, 2, true);
    CHECK(drain(input, fds[1]).total.pressed == NBT_CONTROLLER_OPTION);
    feed.link(false);
    s = drain(input, fds[1]);
    CHECK(s.total.released == NBT_CONTROLLER_OPTION && !input.state().connected);

    // Out-of-sequence and malformed datagrams are ignored.
    Event e;
    uint8_t junk[NBT_CONTROLLER_WIRE_SIZE] = {0};
    CHECK(!input.feed(junk, sizeof(junk), &e));
    close(fds[0]);
    close(fds[1]);
}

} // namespace

int main() {
    wire();
    controls();
    feedAndRead();
    puts("controller feed tests passed");
    return 0;
}
