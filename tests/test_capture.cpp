// SPDX-License-Identifier: MPL-2.0
/* Unit tests for input-capture control record, classifier, 5s escape, and diag ring. */
#include "nbt/capture/classifier.h"
#include "nbt/capture/control.h"
#include "nbt/capture/diag.h"
#include "nbt/capture/route.h"
#include "nbt/ccs.h"
#include "nbt/gui/capture_watch.hpp"
#include "nbt/process/hmi_environment.hpp"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static size_t build_dsi_most(unsigned char *out, uint32_t hdr2, uint32_t tx_type, uint32_t vtag,
                             uint32_t vval, uint8_t fblock, uint8_t inst, uint16_t fkt, uint8_t op,
                             const unsigned char *data, uint32_t data_len) {
    memset(out, 0, 48 + data_len);
    out[8] = (unsigned char)hdr2;
    out[9] = (unsigned char)(hdr2 >> 8);
    out[10] = (unsigned char)(hdr2 >> 16);
    out[11] = (unsigned char)(hdr2 >> 24);
    unsigned char *m = out + 16;
    m[0] = (unsigned char)tx_type;
    m[4] = (unsigned char)vtag;
    m[8] = (unsigned char)vval;
    m[16] = fblock;
    m[17] = inst;
    m[18] = (unsigned char)fkt;
    m[19] = (unsigned char)(fkt >> 8);
    m[20] = op;
    m[28] = (unsigned char)data_len;
    m[29] = (unsigned char)(data_len >> 8);
    if (data_len && data) {
        memcpy(m + 32, data, data_len);
    }
    return 48 + data_len;
}

static int test_control_format_parse_and_shmem(void) {
    nbt_ccs_config ccs;
    nbt_ccs_defaults(&ccs);
    ccs.knob_instance = 0x05;
    ccs.knob_back_slot = 3;
    nbt_capture_control c, parsed;
    nbt_capture_control_init(&c, 1234, 99u, 7u, &ccs);
    c.state = NBT_CAPTURE_ON;
    c.watchdog_ms = 1000u;
    c.escape_enabled = 0;

    char buf[NBT_CAPTURE_CONTROL_MAX];
    const int n = nbt_capture_control_format(&c, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(nbt_capture_control_parse(buf, (size_t)n, &parsed) == 0);
    CHECK(parsed.gui_pid == 1234 && parsed.session_id == 99u && parsed.generation == 7u);
    CHECK(parsed.state == NBT_CAPTURE_ON && parsed.knob_instance == 0x05 &&
          parsed.knob_back_slot == 3 && parsed.watchdog_ms == 1000u && parsed.escape_enabled == 0);

    /* Reject malformed / truncated / leading-zero / trailing bytes. */
    CHECK(nbt_capture_control_parse(buf, (size_t)n - 1, &parsed) != 0);
    char dup[NBT_CAPTURE_CONTROL_MAX];
    memcpy(dup, buf, (size_t)n + 1);
    dup[n - 1] = 'x';
    CHECK(nbt_capture_control_parse(dup, (size_t)n, &parsed) != 0);
    CHECK(nbt_capture_control_parse("v1 pid=01 sid=1 gen=1 st=0 k=03 c=01 inst=01,03 kb=1 cb=1 "
                                    "wd=1500 esc=1\n",
                                    61, &parsed) != 0);

    char dir[] = "/tmp/nbt-cap-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char ctrl_path[256], sym_path[256], live_path[256];
    snprintf(ctrl_path, sizeof(ctrl_path), "%s/ctrl", dir);
    snprintf(sym_path, sizeof(sym_path), "%s/sym", dir);
    snprintf(live_path, sizeof(live_path), "%s/live", dir);
    CHECK(nbt_capture_control_write(ctrl_path, &c) == 0);
    memset(&parsed, 0, sizeof(parsed));
    CHECK(nbt_capture_control_read(ctrl_path, &parsed) == 0);
    CHECK(parsed.generation == 7u && parsed.watchdog_ms == 1000u);

    /* Symlink and world-writable files must be rejected. */
    CHECK(symlink(ctrl_path, sym_path) == 0);
    CHECK(nbt_capture_control_read(sym_path, &parsed) != 0);
    CHECK(chmod(ctrl_path, 0622) == 0);
    CHECK(nbt_capture_control_read(ctrl_path, &parsed) != 0);

    FILE *lf = fopen(live_path, "w");
    CHECK(lf != NULL);
    fprintf(lf, "1234 50000\n");
    fclose(lf);
    CHECK(chmod(live_path, 0600) == 0);
    uint64_t ts = 0;
    CHECK(nbt_capture_liveness_read(live_path, 1234, &ts) == 0 && ts == 50000u);
    CHECK(nbt_capture_liveness_read(live_path, 9999, &ts) != 0);

    unlink(ctrl_path);
    unlink(sym_path);
    unlink(live_path);
    rmdir(dir);
    return 0;
}

static int test_classifier_and_escape(void) {
    nbt_capture_control ctrl;
    nbt_capture_control_init(&ctrl, 42, 10u, 5u, NULL);
    ctrl.state = NBT_CAPTURE_ON;

    unsigned char raw[128];
    unsigned char rot[5] = {0, 0, 1, 0, 0};
    size_t len = build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0xE5, 0x03, 0x0C46, 0x0C, rot, 5);
    nbt_dsi_c_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.capacity = (uint32_t)len;
    stream.populated = (uint32_t)len;
    stream.buffer = raw;

    nbt_capture_packet pkt;
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && pkt.is_candidate &&
          !pkt.has_hard_back);

    /* Segmented, error, and unrelated messages must NOT become candidates. */
    stream.flags = 1u;
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && !pkt.is_candidate);
    stream.flags = 0u;
    size_t seg_len = build_dsi_most(raw, 0x80000001u, 1, 2, 19, 0xE5, 0x03, 0x0C46, 0x0C, rot, 5);
    stream.populated = (uint32_t)seg_len;
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && !pkt.is_candidate);
    size_t other_fkt = build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0xE5, 0x03, 0x0C48, 0x0C, rot, 5);
    stream.populated = (uint32_t)other_fkt;
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && !pkt.is_candidate);

    /* Favourite keys (centre stack 0x15 C02), any instance, press and release:
     * held. Its volume button (C01) and the master volume (0x20) are not. */
    unsigned char fav[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    stream.capacity = (uint32_t)sizeof(raw);
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0x15, 0x07, 0x0C02, 0x0C, fav, 8);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && pkt.is_candidate &&
          !pkt.has_hard_back);
    fav[2] = 1;
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0x15, 0x01, 0x0C02, 0x0C, fav, 8);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && pkt.is_candidate);
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0x15, 0x01, 0x0C01, 0x0C, fav, 8);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && !pkt.is_candidate);
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0x15, 0x01, 0x0C02, 0x01, fav, 8);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && !pkt.is_candidate);
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0x20, 0x01, 0x0C08, 0x0C, fav, 8);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && !pkt.is_candidate);

    /* Tilt-left on 0xC46 is NOT hard BACK; only slot 1 on 0xC47/0xC4F is. */
    unsigned char tilt_left[5] = {0, 0, 0, 0, 10};
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0xE5, 0x03, 0x0C46, 0x0C, tilt_left, 5);
    nbt_capture_escape_state esc;
    nbt_capture_escape_init(&esc);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && pkt.is_candidate &&
          !pkt.has_hard_back);
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 1000, &esc) == NBT_CAP_DECISION_SUPPRESS);
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 7000, &esc) == NBT_CAP_DECISION_SUPPRESS);

    /* Hard BACK press at 8000ms, then release at 13000ms -> release triggers escape! */
    unsigned char back_down[7] = {0, 1, 0, 0, 0, 0, 0};
    unsigned char back_up[7] = {0};
    stream.capacity = (uint32_t)sizeof(raw);
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0xE5, 0x03, 0x0C47, 0x0C, back_down, 7);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && pkt.has_hard_back &&
          pkt.hard_back_held);
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 1, 8000, &esc) == NBT_CAP_DECISION_FORWARD);
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 8000, &esc) == NBT_CAP_DECISION_SUPPRESS);
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0xE5, 0x03, 0x0C47, 0x0C, back_up, 7);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && pkt.has_hard_back &&
          !pkt.hard_back_held);
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 13000, &esc) == NBT_CAP_DECISION_ESCAPE);
    CHECK(esc.held_ms == 5000u); /* the escape record's "BACK held 5.0 s" */

    /* Generation replay (gen <= 5) stays latched off; new generation (6) re-enables. */
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 14000, &esc) == NBT_CAP_DECISION_FORWARD);
    ctrl.generation = 4u;
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 14100, &esc) == NBT_CAP_DECISION_FORWARD);
    ctrl.generation = 6u;
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 14200, &esc) == NBT_CAP_DECISION_SUPPRESS);

    /* A BACK hold begun in generation 6 whose release went by uncaptured: the
     * first message of generation 7, 10 s later, is held, not an escape (car,
     * 2026-11-18: a knob push escaped and OPTION then reached iDrive). */
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0xE5, 0x03, 0x0C47, 0x0C, back_down, 7);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0 && pkt.hard_back_held);
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 15000, &esc) == NBT_CAP_DECISION_SUPPRESS);
    ctrl.generation = 7u;
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0xE5, 0x03, 0x0C46, 0x0C, tilt_left, 5);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0);
    esc.dropped = 0;
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 25000, &esc) == NBT_CAP_DECISION_SUPPRESS);
    /* ... and the dropped hold is reported for the record, with its age. */
    CHECK(esc.dropped && esc.dropped_generation == 6u && esc.held_ms == 10000u);

    /* escape_enabled = 0 (during active install) ignores 5s hold. */
    ctrl.escape_enabled = 0;
    stream.capacity = (uint32_t)sizeof(raw);
    stream.populated =
        (uint32_t)build_dsi_most(raw, 0x80000001u, 0, 2, 1, 0xE5, 0x03, 0x0C47, 0x0C, back_down, 7);
    CHECK(nbt_capture_inspect_stream(&stream, &ctrl, &pkt) == 0);
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 20000, &esc) == NBT_CAP_DECISION_SUPPRESS);
    CHECK(nbt_capture_evaluate(&pkt, &ctrl, 1, 0, 27000, &esc) == NBT_CAP_DECISION_SUPPRESS);

    /* Hard vs tilt separation in nbt_ccs_back_key_held */
    nbt_ccs ccs_state;
    nbt_ccs_init(&ccs_state, NULL);
    nbt_input out[16];
    nbt_ccs_feed(&ccs_state, 0xE5, 0x03, 0x0C46, 0x0C, tilt_left, 5, out, 16);
    CHECK(nbt_ccs_back_held(&ccs_state) == 1 && nbt_ccs_back_key_held(&ccs_state) == 0);
    nbt_ccs_feed(&ccs_state, 0xE5, 0x03, 0x0C47, 0x0C, back_down, 7, out, 16);
    CHECK(nbt_ccs_back_key_held(&ccs_state) == 1);
    return 0;
}

/* The GUI's note: escaped for its generation only, observer, or no live shim. */
static int test_capture_watch(void) {
    using nbt::gui::CaptureWatch;
    nbt_capture_diag_shm snap;
    memset(&snap, 0, sizeof(snap));
    snap.magic = NBT_CAPTURE_DIAG_MAGIC;
    snap.version = NBT_CAPTURE_DIAG_VERSION;
    snap.pid = 1;
    snap.slots[3].seq = snap.slots[3].seq_end = 3;
    snap.slots[3].kind = NBT_CAPTURE_DIAG_ESCAPE;
    snap.slots[3].generation = 41;
    CHECK(CaptureWatch::classify(&snap, 42, true) == CaptureWatch::Blocking);
    CHECK(CaptureWatch::classify(&snap, 41, true) == CaptureWatch::Escaped);
    snap.slots[3].seq_end = 2; /* torn slot: not read */
    CHECK(CaptureWatch::classify(&snap, 41, true) == CaptureWatch::Blocking);
    CHECK(CaptureWatch::classify(&snap, 41, false) == CaptureWatch::NotLoaded);
    CHECK(CaptureWatch::classify(NULL, 41, true) == CaptureWatch::NotLoaded);
    snap.observer_mode = 1;
    CHECK(CaptureWatch::classify(&snap, 41, true) == CaptureWatch::Observer);
    CaptureWatch watch;
    CHECK(!watch.poll(NULL, 41, true, 0) && !watch.note()[0]); /* not watched */
    return 0;
}

static uint32_t g_seen = 0;
static void count_slot(const nbt_capture_diag_slot *slot, void *) {
    if (slot->kind == NBT_CAPTURE_DIAG_OBSERVE) {
        ++g_seen;
    }
}

static int test_diag_ring_overrun(void) {
    nbt_capture_diag_shm shm;
    memset(&shm, 0, sizeof(shm));
    shm.magic = NBT_CAPTURE_DIAG_MAGIC;
    shm.version = NBT_CAPTURE_DIAG_VERSION;
    shm.observer_mode = 1;
    shm.pid = 77;
    for (uint32_t seq = 1; seq <= NBT_CAPTURE_DIAG_SLOTS + 5u; ++seq) {
        nbt_capture_diag_slot *s = &shm.slots[seq % NBT_CAPTURE_DIAG_SLOTS];
        s->seq = seq;
        s->seq_end = seq;
        s->kind = NBT_CAPTURE_DIAG_OBSERVE;
        s->timestamp_ms = 1000u + seq;
    }
    shm.write_seq = NBT_CAPTURE_DIAG_SLOTS + 5u;
    uint32_t cursor = 0, missed = 0;
    nbt_capture_diag_drain(&shm, &cursor, &missed, count_slot, NULL);
    CHECK(missed == 5u && g_seen == NBT_CAPTURE_DIAG_SLOTS &&
          cursor == NBT_CAPTURE_DIAG_SLOTS + 5u);
    return 0;
}

/* The hidden audio connection: its note, and ConnectionMaster's table without it. */
static int test_route_note_and_table(void) {
    char text[64];
    int32_t pid = 0;
    uint16_t conn = 0;
    const int n = nbt_capture_route_format(4242, 0x1d0, text, sizeof(text));
    CHECK(n > 0 && !strcmp(text, "NBTROUTE1 4242 1d0\n"));
    CHECK(nbt_capture_route_parse(text, (size_t)n, &pid, &conn) == 0 && pid == 4242 &&
          conn == 0x1d0);
    static const char *const bad[] = {
        "NBTROUTE1 4242 1d0",     "NBTROUTE1 0 1d0\n",     "NBTROUTE1 4242 0\n",
        "NBTROUTE1 4242 1D0\n",   "NBTROUTE1 4242 1d0 \n", "NBTROUTE2 4242 1d0\n",
        "NBTROUTE1 4242 12345\n", "NBTROUTE1 -1 1d0\n",    "NBTROUTE1 99999999999 1d0\n",
        "NBTROUTE1 042 1d0\n",    "NBTROUTE1 42 01d0\n"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(nbt_capture_route_parse(bad[i], strlen(bad[i]), &pid, &conn) != 0);
    }
    CHECK(nbt_capture_route_format(0, 0x1d0, text, sizeof(text)) < 0 &&
          nbt_capture_route_format(1, 0, text, sizeof(text)) < 0 &&
          nbt_capture_route_format(4242, 0x1d0, text, 8) < 0);
    char path[] = "/tmp/nbt-route-XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0 && write(fd, "NBTROUTE1 77 b8\n", 16) == 16 && close(fd) == 0);
    CHECK(nbt_capture_route_read(path, &pid, &conn) == 0 && pid == 77 && conn == 0xb8);
    CHECK(chmod(path, 0666) == 0 && nbt_capture_route_read(path, &pid, &conn) != 0);
    CHECK(unlink(path) == 0 && nbt_capture_route_read(path, &pid, &conn) != 0);

    /* The car's table with the media connection built (2026-11-27): the phone's
     * reference channel 0x1c1, then 0x1d0. */
    unsigned char buf[160];
    const unsigned char both[] = {0, 0, 0x01, 0xC1, 0x00, 0x01, 0xD0, 0x00};
    size_t len = build_dsi_most(buf, 0x80000001u, 0, 2, 1, 0x03, 0x00, 0x0C03, 0x0C, both, 8);
    unsigned char before[160];
    memcpy(before, buf, len);
    /* Reported only, not a byte changed (the observer build). */
    CHECK(nbt_capture_table_hide(buf, len, 0x1d0, 0) == NBT_CAPTURE_TABLE_REWRITTEN &&
          !memcmp(buf, before, len));
    CHECK(nbt_capture_table_hide(buf, len, 0x1d0, 1) == NBT_CAPTURE_TABLE_REWRITTEN);
    const unsigned char one[] = {0, 0, 0x01, 0xC1, 0x00, 0, 0, 0};
    CHECK(buf[16 + 28] == 5 && !memcmp(buf + 48, one, 8) && !memcmp(buf, before, 16 + 28));
    /* Now as iDrive knew it before: nothing left to take out. */
    CHECK(nbt_capture_table_hide(buf, len, 0x1d0, 1) == NBT_CAPTURE_TABLE_NONE);
    /* Rows on both sides stay in order; a table of that connection alone is empty. */
    const unsigned char three[] = {0,    0,    0x01, 0xD0, 0x00, 0x00, 0x05,
                                   0x01, 0x01, 0xD0, 0x02, 0x01, 0xC1, 0x00};
    len = build_dsi_most(buf, 0x80000001u, 0, 2, 1, 0x03, 0x00, 0x0C03, 0x0C, three, 14);
    CHECK(nbt_capture_table_hide(buf, len, 0x1d0, 1) == NBT_CAPTURE_TABLE_REWRITTEN);
    const unsigned char two[] = {0, 0, 0x00, 0x05, 0x01, 0x01, 0xC1, 0x00, 0, 0, 0, 0, 0, 0};
    CHECK(buf[16 + 28] == 8 && !memcmp(buf + 48, two, 14));
    const unsigned char alone[] = {0, 0, 0x01, 0xD0, 0x00};
    len = build_dsi_most(buf, 0x80000001u, 0, 2, 1, 0x03, 0x00, 0x0C03, 0x0C, alone, 5);
    CHECK(nbt_capture_table_hide(buf, len, 0x1d0, 1) == NBT_CAPTURE_TABLE_REWRITTEN &&
          buf[16 + 28] == 2);
    /* A row sent alone (position 2): held when it is that connection's. */
    const unsigned char row[] = {2, 0, 0x01, 0xD0, 0x00};
    len = build_dsi_most(buf, 0x80000001u, 0, 2, 1, 0x03, 0x00, 0x0C03, 0x0C, row, 5);
    memcpy(before, buf, len);
    CHECK(nbt_capture_table_hide(buf, len, 0x1d0, 1) == NBT_CAPTURE_TABLE_ROW &&
          !memcmp(buf, before, len));
    CHECK(nbt_capture_table_hide(buf, len, 0x1c1, 1) == NBT_CAPTURE_TABLE_NONE);
    /* Everything else goes by untouched: another function, operation, FBlock or
     * client message, a length that is no table, more rows than iDrive reads,
     * a length beyond the stream. */
    struct {
        uint32_t hdr2, vval;
        uint8_t fblock;
        uint16_t fkt;
        uint8_t op;
        uint32_t data_len;
    } const other[] = {
        {0x80000001u, 1, 0x03, 0x0C02, 0x0C, 8}, {0x80000001u, 1, 0x03, 0x0C03, 0x01, 8},
        {0x80000001u, 1, 0x20, 0x0C03, 0x0C, 8}, {0x80000002u, 1, 0x03, 0x0C03, 0x0C, 8},
        {0x80000001u, 5, 0x03, 0x0C03, 0x0C, 8}, {0x80000001u, 1, 0x03, 0x0C03, 0x0C, 7},
        {0x80000001u, 1, 0x03, 0x0C03, 0x0C, 2}};
    for (size_t i = 0; i < sizeof(other) / sizeof(other[0]); ++i) {
        len = build_dsi_most(buf, other[i].hdr2, 0, 2, other[i].vval, other[i].fblock, 0x00,
                             other[i].fkt, other[i].op, both, other[i].data_len);
        memcpy(before, buf, len);
        CHECK(nbt_capture_table_hide(buf, len, 0x1d0, 1) == NBT_CAPTURE_TABLE_NONE &&
              !memcmp(buf, before, len));
    }
    unsigned char many[2 + 21 * 3];
    memset(many, 0, sizeof(many));
    for (size_t r = 0; r < 21; ++r) {
        many[2 + 3 * r] = 0x01;
        many[3 + 3 * r] = 0xD0;
    }
    len = build_dsi_most(buf, 0x80000001u, 0, 2, 1, 0x03, 0x00, 0x0C03, 0x0C, many, sizeof(many));
    CHECK(nbt_capture_table_hide(buf, len, 0x1d0, 1) == NBT_CAPTURE_TABLE_NONE);
    len = build_dsi_most(buf, 0x80000001u, 0, 2, 1, 0x03, 0x00, 0x0C03, 0x0C, both, 8);
    CHECK(nbt_capture_table_hide(buf, len - 1, 0x1d0, 1) == NBT_CAPTURE_TABLE_NONE &&
          nbt_capture_table_hide(buf, 40, 0x1d0, 1) == NBT_CAPTURE_TABLE_NONE &&
          nbt_capture_table_hide(NULL, len, 0x1d0, 1) == NBT_CAPTURE_TABLE_NONE &&
          nbt_capture_table_hide(buf, len, 0, 1) == NBT_CAPTURE_TABLE_NONE);
    return 0;
}

int main(void) {
    if (test_control_format_parse_and_shmem() || test_classifier_and_escape() ||
        test_diag_ring_overrun() || test_capture_watch() || test_route_note_and_table()) {
        return 1;
    }
    puts("capture-test: OK");
    return 0;
}
