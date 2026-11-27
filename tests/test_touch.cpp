// SPDX-License-Identifier: MPL-2.0
/* The touch screen's datagrams and the manager's gestures
 * (include/nbt/gui/touch_input.hpp). */
#include "nbt/gui/touch_input.hpp"

#include <stdio.h>
#include <string.h>

using nbt::gui::TouchContact;
using nbt::gui::TouchFrames;
using nbt::gui::TouchGesture;
using nbt::gui::TouchSample;
using nbt::gui::TouchTracker;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

/* One message of 12 records, as the touch controller sends it, in datagrams
 * of `split[i]` records each (0 ends the list); returns the datagrams' sizes
 * in `sizes` and their bytes one after the other in `out`. */
static size_t build(const TouchContact *contacts, size_t count, uint8_t first, const size_t *split,
                    uint8_t out[4][112], size_t sizes[4]) {
    uint8_t stream[32];
    memset(stream, 0, sizeof(stream));
    stream[0] = 0x40;
    stream[1] = static_cast<uint8_t>(count);
    for (size_t c = 0; c < count && c < 5; ++c) {
        uint8_t *p = stream + 2 + 6 * c;
        p[0] = static_cast<uint8_t>(contacts[c].x >> 8);
        p[1] = static_cast<uint8_t>(contacts[c].x);
        p[2] = static_cast<uint8_t>(contacts[c].y >> 8);
        p[3] = static_cast<uint8_t>(contacts[c].y);
        p[4] = 9;
        p[5] = static_cast<uint8_t>(contacts[c].state << 4 | contacts[c].id);
    }
    size_t record = 0, datagrams = 0;
    for (; record < 12 && split[datagrams]; ++datagrams) {
        uint8_t *d = out[datagrams];
        const size_t records = record + split[datagrams] > 12 ? 12 - record : split[datagrams];
        memset(d, 0, 112);
        d[0] = 0xB0;
        d[1] = 0x04;
        d[2] = 0x80;
        d[3] = 0x02;
        d[7] = static_cast<uint8_t>(8 + 8 * records);
        for (size_t r = 0; r < records; ++r, ++record) {
            uint8_t *rec = d + 16 + 8 * r;
            rec[1] = record == 0 ? 0xC5 : record == 11 ? 0xC3 : 0xC1;
            rec[2] = 0x40;
            rec[3] = static_cast<uint8_t>(first + record);
            if (record >= 4) {
                memcpy(rec + 4, stream + 4 * (record - 4), 4);
            }
        }
        sizes[datagrams] = 16 + 8 * records;
    }
    return datagrams;
}

static int test_frames(void) {
    uint8_t dg[4][112];
    size_t sizes[4];
    TouchSample samples[4];
    TouchFrames frames;
    /* The Supra: a whole message in one 112-byte datagram, two fingers. */
    const TouchContact two[] = {{0, 1, 640, 240}, {1, 2, 1279, 479}};
    const size_t whole[] = {12, 0};
    CHECK(build(two, 2, 0, whole, dg, sizes) == 1 && sizes[0] == 112);
    CHECK(frames.feed(dg[0], sizes[0], samples, 4) == 1 && samples[0].count == 2);
    CHECK(samples[0].contacts[0].x == 640 && samples[0].contacts[0].y == 240 &&
          samples[0].contacts[0].state == 1 && samples[0].contacts[0].id == 0);
    CHECK(samples[0].contacts[1].x == 1279 && samples[0].contacts[1].y == 479 &&
          samples[0].contacts[1].state == 2 && samples[0].contacts[1].id == 1);
    /* A 1440x540 unit: the same message over three datagrams (2 + 5 + 5). */
    const TouchContact one[] = {{0, 2, 1439, 539}};
    const size_t three[] = {2, 5, 5, 0};
    CHECK(build(one, 1, 12, three, dg, sizes) == 3 && sizes[0] == 32 && sizes[1] == 56);
    CHECK(frames.feed(dg[0], sizes[0], samples, 4) == 0 &&
          frames.feed(dg[1], sizes[1], samples, 4) == 0 &&
          frames.feed(dg[2], sizes[2], samples, 4) == 1);
    CHECK(samples[0].count == 1 && samples[0].contacts[0].x == 1439 &&
          samples[0].contacts[0].y == 539);
    /* A lost datagram drops that message; the next one is read again. */
    CHECK(build(one, 1, 24, three, dg, sizes) == 3);
    CHECK(frames.feed(dg[0], sizes[0], samples, 4) == 0 &&
          frames.feed(dg[2], sizes[2], samples, 4) == 0);
    CHECK(build(one, 1, 36, three, dg, sizes) == 3);
    CHECK(frames.feed(dg[0], sizes[0], samples, 4) == 0 &&
          frames.feed(dg[1], sizes[1], samples, 4) == 0 &&
          frames.feed(dg[2], sizes[2], samples, 4) == 1);
    /* No finger: an empty sample. Unknown states and ids are left out. */
    CHECK(build(NULL, 0, 48, whole, dg, sizes) == 1 &&
          frames.feed(dg[0], sizes[0], samples, 4) == 1 && samples[0].count == 0);
    const TouchContact odd[] = {{0, 7, 1, 1}, {9, 1, 2, 2}, {2, 3, 30, 40}};
    CHECK(build(odd, 3, 60, whole, dg, sizes) == 1 &&
          frames.feed(dg[0], sizes[0], samples, 4) == 1 && samples[0].count == 1 &&
          samples[0].contacts[0].id == 2 && samples[0].contacts[0].state == 3);
    /* More contacts declared than a message holds: five at most, no overrun. */
    CHECK(build(two, 2, 72, whole, dg, sizes) == 1);
    dg[0][16 + 8 * 4 + 4 + 1] = 200;
    CHECK(frames.feed(dg[0], sizes[0], samples, 4) == 1 && samples[0].count <= 5);
    /* Not the touch controller's: another ident (the gesture camera), a short
     * datagram, a record that is none. */
    CHECK(build(two, 2, 84, whole, dg, sizes) == 1);
    dg[0][1] = 0x09;
    CHECK(frames.feed(dg[0], sizes[0], samples, 4) == -1);
    dg[0][1] = 0x04;
    CHECK(frames.feed(dg[0], 20, samples, 4) == -1 && frames.feed(NULL, 112, samples, 4) == -1);
    dg[0][16 + 1] = 0x05;
    CHECK(frames.feed(dg[0], sizes[0], samples, 4) == -1);
    /* A message of more records than any is dropped, not written past the end. */
    uint8_t longDatagram[16 + 8 * 60];
    memset(longDatagram, 0, sizeof(longDatagram));
    memcpy(longDatagram, dg[0], 4);
    longDatagram[6] = static_cast<uint8_t>((sizeof(longDatagram) - 8) >> 8);
    longDatagram[7] = static_cast<uint8_t>(sizeof(longDatagram) - 8);
    for (size_t r = 0; r < 60; ++r) {
        longDatagram[16 + 8 * r + 1] = r == 0 ? 0xC5 : r == 59 ? 0xC3 : 0xC1;
        longDatagram[16 + 8 * r + 3] = static_cast<uint8_t>(r);
    }
    CHECK(frames.feed(longDatagram, sizeof(longDatagram), samples, 4) == 0);
    return 0;
}

static TouchSample at(uint8_t state, int x, int y, uint8_t id = 0) {
    TouchSample s;
    memset(&s, 0, sizeof(s));
    s.count = 1;
    s.contacts[0].id = id;
    s.contacts[0].state = state;
    s.contacts[0].x = static_cast<uint16_t>(x);
    s.contacts[0].y = static_cast<uint16_t>(y);
    return s;
}

static int test_gestures(void) {
    TouchTracker tracker(60, 24, 320); /* a row, a tap's slop, BACK's travel */
    TouchSample none;
    memset(&none, 0, sizeof(none));
    /* Down and up in place: a tap where it went down. */
    CHECK(tracker.feed(at(1, 400, 200)).kind == TouchGesture::None);
    CHECK(tracker.feed(at(2, 405, 210)).kind == TouchGesture::None);
    TouchGesture g = tracker.feed(at(3, 405, 210));
    CHECK(g.kind == TouchGesture::Tap && g.x == 400 && g.y == 200);
    /* The finger gone from the samples without an "up" is a tap too. */
    CHECK(tracker.feed(at(1, 100, 100)).kind == TouchGesture::None);
    g = tracker.feed(none);
    CHECK(g.kind == TouchGesture::Tap && g.x == 100 && g.y == 100);
    /* Dragged up by two and a half rows: two steps on, then one back down; no tap. */
    CHECK(tracker.feed(at(1, 400, 400)).kind == TouchGesture::None);
    g = tracker.feed(at(2, 402, 250));
    CHECK(g.kind == TouchGesture::Step && g.steps == 2);
    CHECK(tracker.feed(at(2, 402, 240)).kind == TouchGesture::None);
    g = tracker.feed(at(2, 402, 345));
    CHECK(g.kind == TouchGesture::Step && g.steps == -1);
    CHECK(tracker.feed(at(3, 402, 345)).kind == TouchGesture::None);
    /* A swipe to the right: BACK, once, and no tap after. */
    CHECK(tracker.feed(at(1, 100, 300)).kind == TouchGesture::None);
    CHECK(tracker.feed(at(2, 300, 310)).kind == TouchGesture::None);
    CHECK(tracker.feed(at(2, 430, 320)).kind == TouchGesture::Back);
    CHECK(tracker.feed(at(2, 600, 320)).kind == TouchGesture::None);
    CHECK(tracker.feed(at(3, 600, 320)).kind == TouchGesture::None);
    /* To the left, or sideways and back: nothing. */
    CHECK(tracker.feed(at(1, 600, 300)).kind == TouchGesture::None);
    CHECK(tracker.feed(at(2, 200, 300)).kind == TouchGesture::None);
    CHECK(tracker.feed(at(3, 200, 300)).kind == TouchGesture::None);
    /* A second finger changes nothing: the first one leads. */
    TouchSample two = at(1, 400, 200, 0);
    CHECK(tracker.feed(two).kind == TouchGesture::None);
    two.count = 2;
    two.contacts[0].state = 2;
    two.contacts[1] = at(1, 900, 100, 1).contacts[0];
    CHECK(tracker.feed(two).kind == TouchGesture::None);
    two.contacts[1].state = 3;
    CHECK(tracker.feed(two).kind == TouchGesture::None);
    two.count = 1;
    two.contacts[0].state = 3;
    g = tracker.feed(two);
    CHECK(g.kind == TouchGesture::Tap && g.x == 400);
    /* reset(): a finger still down when the screen went is forgotten. */
    CHECK(tracker.feed(at(1, 400, 200)).kind == TouchGesture::None);
    tracker.reset();
    CHECK(tracker.feed(at(3, 400, 200)).kind == TouchGesture::None);
    return 0;
}

int main(void) {
    if (test_frames() || test_gestures()) {
        return 1;
    }
    puts("touch-test: OK");
    return 0;
}
