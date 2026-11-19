/* MOST bridge building blocks: policy, wire records, segment joining and the
 * requestProcessMessage payload. */
#include "nbt/most.h"
#include "nbt/most/most_session.hpp"
#include "nbt/most/subscriptions.hpp"
#include "nbt/most/volume.hpp"
#include "nbt/most_policy.h"
#include "nbt/most_wire.h"
#include "nbt/worker/android_auto_wifi.hpp"
#include "nbt/worker/wfd_health.hpp"
#include "nbt/worker/wifi_direct_most.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void policy(void) {
    const char *bad[] = {
        "",
        ",",
        "rx:05.00.621,",
        ",rx:05.00.621",
        "rx:05.00.621,,tx:82.00.*",
        "rx:5.00.621",
        "rx:05.0.621",
        "rx:05.00.62",
        "rx:05.00.6210",
        "rx:05.00.62g",
        "rx:05.00.62a",
        "RX:05.00.621",
        "rx:05:00:621",
        "rx:05.00",
        "rx:05.00.621.1",
        "tx:*.*.*",
        "tx:*.00.621",
        "rx:*.00.*",
        "rx:*.*.621",
        "ctl:E5",
        "ctl:E5.*",
        "ctl:*.03",
        "ctl:E5.03.000",
        "ctl:E5.03,ctl:E5.03",
        "rx:05.00.621,rx:05.00.621",
        "xx:05.00.621",
        "rx:05.00.621 ",
        " rx:05.00.621",
        "rx:05.00.*,rx:05.00.*",
        "tx:05.00.000",
        "tx:05.*.001",
        "tx:05.00.002",
        "sub:05.00.621",
        "rx:05.00.*,sub:05.*.621",
        "rx:*.*.*,sub:*.00.621",
        "rx:*.*.*,sub:05.00.*",
        "rx:*.*.*,sub:05.00.001",
        "rx:*.*.*,sub:01.00.100",
        "rx:05.00.620,sub:05.00.621",
        "rx:05.01.621,sub:05.00.621",
        "rx:05.00.621,sub:05.00.621,sub:05.00.621",
    };
    char text[NBT_MOST_POLICY_MAX + 2];
    nbt_most_policy p;
    uint16_t keys[NBT_MOST_POLICY_CONTROLS];
    unsigned i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        if (!nbt_most_policy_parse(bad[i], NULL)) {
            fprintf(stderr, "accepted: %s\n", bad[i]);
        }
        assert(nbt_most_policy_parse(bad[i], NULL));
    }
    assert(nbt_most_policy_parse(NULL, &p));
    assert(!nbt_most_policy_parse("sub:05.00.621,rx:05.*.*", &p));
    assert(!nbt_most_policy_receives(&p, 0x06, 0, 0x621));
    assert(!nbt_most_policy_sends(&p, 0x05, 0, 0x621));
    strcpy(text, "rx:*.*.*");
    for (i = 0; i < 33; ++i) {
        sprintf(text + strlen(text), ",sub:05.00.%03X", i + 3);
    }
    assert(nbt_most_policy_parse(text, NULL));
    *strrchr(text, ',') = 0;
    assert(!nbt_most_policy_parse(text, NULL));
    /* NBT-AAD's needs, as one policy. */
    assert(!nbt_most_policy_parse(
        "rx:E5.*.*,rx:15.*.*,rx:05.00.621,rx:05.00.626,rx:05.00.228,rx:05.00.CB5,"
        "rx:52.00.440,rx:CD.01.C00,rx:CD.01.203,rx:20.*.C08,rx:82.00.*,tx:82.00.*,"
        "ctl:E5.10,ctl:E5.03,ctl:E5.05,ctl:E5.12,ctl:E5.14,ctl:E5.01,ctl:E5.02,ctl:15.02",
        &p));
    assert(p.count == 20);
    assert(nbt_most_policy_receives(&p, 0xe5, 0x77, 0x123));
    assert(nbt_most_policy_receives(&p, 0x05, 0x00, 0xcb5));
    assert(!nbt_most_policy_receives(&p, 0x05, 0x01, 0xcb5));
    assert(!nbt_most_policy_receives(&p, 0x05, 0x00, 0xcb6));
    assert(nbt_most_policy_receives(&p, 0x20, 0x03, 0xc08));
    assert(nbt_most_policy_sends(&p, 0x82, 0x00, 0x400));
    for (unsigned f = 0; f <= 2; ++f) {
        assert(!nbt_most_policy_sends(&p, 0x82, 0x00, f));
    }
    assert(!nbt_most_policy_sends(&p, 0x82, 0x01, 0x400));
    /* rx does not imply tx, nor tx rx. */
    assert(!nbt_most_policy_sends(&p, 0x05, 0x00, 0x621));
    assert(nbt_most_policy_controls(&p, keys) == 8 && keys[0] == 0xe510 && keys[7] == 0x1502);
    assert(!nbt_most_policy_receives(NULL, 0, 0, 0) && !nbt_most_policy_sends(NULL, 0, 0, 0));
    assert(!nbt_most_policy_parse("rx:*.*.*", &p) &&
           nbt_most_policy_receives(&p, 0xff, 0xff, 0xfff));
    assert(!nbt_most_policy_sends(&p, 0xff, 0xff, 0xfff) && !nbt_most_policy_controls(&p, keys));
    /* Bounds: 48 rules, 16 controls, 640 bytes. */
    text[0] = 0;
    for (i = 0; i < 49; ++i) {
        sprintf(text + strlen(text), "%srx:%02X.00.000", i ? "," : "", i);
    }
    assert(nbt_most_policy_parse(text, NULL));
    *strrchr(text, ',') = 0;
    assert(!nbt_most_policy_parse(text, &p) && p.count == 48);
    text[0] = 0;
    for (i = 0; i < 17; ++i) {
        sprintf(text + strlen(text), "%sctl:%02X.00", i ? "," : "", i);
    }
    assert(nbt_most_policy_parse(text, NULL));
    *strrchr(text, ',') = 0;
    assert(!nbt_most_policy_parse(text, NULL));
    memset(text, 'x', sizeof(text) - 1);
    text[sizeof(text) - 1] = 0;
    memcpy(text, "rx:05.00.621,", 13);
    assert(nbt_most_policy_parse(text, NULL));
}

static void wire(void) {
    uint8_t record[NBT_MOST_WIRE_RECORD_MAX + 1], data[NBT_MOST_WIRE_DATA_MAX + 1];
    nbt_most_wire_message m, back;
    nbt_most_wire_state state = {NBT_MOST_LINK_UP, 0x1234, 7}, state_back;
    nbt_most_wire_result result = {0xdeadbeefu, NBT_MOST_BUSY}, result_back;
    nbt_most_wire_entry entries[3] = {{0x0100, 0x0500}, {0x0101, 0x8200}, {0xffff, 0xe503}},
                        entries_back[2];
    long n;
    size_t i;
    for (i = 0; i < sizeof(data); ++i) {
        data[i] = (uint8_t)i;
    }
    n = nbt_most_wire_encode_state(&state, record, sizeof(record));
    assert(n == 12 && nbt_most_wire_type(record, 12) == NBT_MOST_WIRE_STATE);
    assert(!nbt_most_wire_decode_state(record, 12, &state_back) &&
           state_back.link == NBT_MOST_LINK_UP && state_back.address == 0x1234 &&
           state_back.dropped == 7);
    assert(nbt_most_wire_decode_state(record, 11, &state_back) &&
           nbt_most_wire_decode_state(record, 13, &state_back));
    record[4] = 5;
    assert(nbt_most_wire_decode_state(record, 12, &state_back));
    record[0] = 2;
    assert(nbt_most_wire_type(record, 12) < 0);
    n = nbt_most_wire_encode_result(&result, record, sizeof(record));
    assert(n == 12 && !nbt_most_wire_decode_result(record, 12, &result_back) &&
           result_back.request == 0xdeadbeefu && result_back.code == NBT_MOST_BUSY);
    record[8] = NBT_MOST_BUSY + 1;
    assert(nbt_most_wire_decode_result(record, 12, &result_back));
    n = nbt_most_wire_encode_registry(entries, 3, record, sizeof(record));
    assert(n == 20 && nbt_most_wire_decode_registry(record, 20, entries_back, 2) == 3);
    assert(entries_back[1].address == 0x0101 && entries_back[1].key == 0x8200);
    assert(nbt_most_wire_decode_registry(record, 19, entries_back, 2) < 0);
    assert(nbt_most_wire_decode_registry(record, 24, entries_back, 2) < 0);
    assert(nbt_most_wire_encode_registry(entries, NBT_MOST_WIRE_REGISTRY_MAX + 1, record,
                                         sizeof(record)) < 0);

    memset(&m, 0, sizeof(m));
    m.src = 0x0101;
    m.dst = 0x1234;
    m.fblock = 0x05;
    m.inst = 0;
    m.fkt = 0x621;
    m.op = 12;
    m.flags = NBT_MOST_FLAG_JOINED;
    m.tag = 0x01020304u;
    m.dropped = 9;
    m.data = data;
    m.length = NBT_MOST_WIRE_DATA_MAX;
    n = nbt_most_wire_encode_message(NBT_MOST_WIRE_MESSAGE, &m, record, sizeof(record));
    assert(n == (long)NBT_MOST_WIRE_RECORD_MAX);
    assert(!nbt_most_wire_decode_message(record, (size_t)n, &back));
    assert(back.src == 0x0101 && back.dst == 0x1234 && back.fkt == 0x621 && back.op == 12 &&
           back.tag == 0x01020304u && back.flags == NBT_MOST_FLAG_JOINED && back.dropped == 9 &&
           back.length == NBT_MOST_WIRE_DATA_MAX && !memcmp(back.data, data, back.length));
    assert(nbt_most_wire_decode_message(record, (size_t)n - 1, &back));
    m.length = NBT_MOST_WIRE_DATA_MAX + 1;
    assert(nbt_most_wire_encode_message(NBT_MOST_WIRE_MESSAGE, &m, record, sizeof(record)) < 0);
    m.length = 3;
    m.fkt = 0x1000;
    assert(nbt_most_wire_encode_message(NBT_MOST_WIRE_MESSAGE, &m, record, sizeof(record)) < 0);
    m.fkt = 0x621;
    m.op = 16;
    assert(nbt_most_wire_encode_message(NBT_MOST_WIRE_MESSAGE, &m, record, sizeof(record)) < 0);
    m.op = 1;
    m.src = 0;
    m.request = 42;
    m.fblock = 0x82;
    m.fkt = 0x400;
    n = nbt_most_wire_encode_message(NBT_MOST_WIRE_SEND, &m, record, sizeof(record));
    assert(n == 27 && nbt_most_wire_type(record, 27) == NBT_MOST_WIRE_SEND);
    assert(!nbt_most_wire_decode_message(record, 27, &back) && back.request == 42 &&
           back.fblock == 0x82 && !back.src);
    record[4] = 1; /* a sender address in SEND */
    assert(nbt_most_wire_decode_message(record, 27, &back));
    record[4] = 0;
    record[2] = 1; /* flags in SEND */
    assert(nbt_most_wire_decode_message(record, 27, &back));
    record[2] = 0;
    record[22] = 1; /* reserved */
    assert(nbt_most_wire_decode_message(record, 27, &back));
    record[22] = 0;
    record[11] = 0x10; /* fkt above 12 bits */
    assert(nbt_most_wire_decode_message(record, 27, &back));
    record[11] = 0x04;
    record[3] = 16;
    assert(nbt_most_wire_decode_message(record, 27, &back));
    assert(nbt_most_wire_type(record, 3) < 0 && nbt_most_wire_type(NULL, 12) < 0);
}

/* A received MessageType with the given class fields. */
static size_t message(uint8_t *out, uint32_t tx, uint32_t variant, uint32_t value, uint16_t fkt,
                      const uint8_t *data, uint32_t length) {
    memset(out, 0, 32);
    out[0] = (uint8_t)tx;
    out[4] = (uint8_t)variant;
    out[8] = (uint8_t)value;
    out[12] = 0x34;
    out[13] = 0x12;
    out[14] = 0x01;
    out[15] = 0x01;
    out[16] = 0x82;
    out[17] = 0;
    out[18] = (uint8_t)fkt;
    out[19] = (uint8_t)(fkt >> 8);
    out[20] = 12;
    out[28] = (uint8_t)length;
    out[29] = (uint8_t)(length >> 8);
    memcpy(out + 32, data, length);
    return 32 + length;
}

static void joining(void) {
    static nbt_most_joiner j;
    uint8_t raw[32 + NBT_MOST_JOIN_MAX], part[NBT_MOST_JOIN_MAX];
    nbt_most_message m, whole;
    size_t n, i;
    int kind;
    for (i = 0; i < sizeof(part); ++i) {
        part[i] = (uint8_t)(i * 7);
    }
    nbt_most_join_init(&j);
    n = message(raw, 0, 2, 0, 0x400, part, 4);
    assert(nbt_most_parse_any(raw, n, &m) == NBT_MOST_NORMAL && m.fblock == 0x82 &&
           m.src == 0x0101 && m.op == 12);
    assert(nbt_most_parse_any(raw, n - 1, &m) < 0);
    n = message(raw, 0, 1, 0, 0x400, part, 4);
    assert(nbt_most_parse_any(raw, n, &m) == NBT_MOST_NO_INFO);
    n = message(raw, 0, 2, 5, 0x400, part, 0);
    assert(nbt_most_parse_any(raw, n, &m) == NBT_MOST_TX_STATUS);
    /* A middle segment without a first one is lost. */
    n = message(raw, 1, 2, 20, 0x400, part, 10);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(kind == NBT_MOST_SEG_MIDDLE && nbt_most_join(&j, &m, (nbt_most_class)kind, &whole) < 0 &&
           j.lost == 1);
    /* first + middle + last of another function interleaved with a second message. */
    n = message(raw, 1, 2, 19, 0x400, part, 10);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(!nbt_most_join(&j, &m, (nbt_most_class)kind, &whole));
    n = message(raw, 1, 2, 19, 0x401, part + 100, 3);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(!nbt_most_join(&j, &m, (nbt_most_class)kind, &whole));
    n = message(raw, 1, 2, 20, 0x400, part + 10, 10);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(!nbt_most_join(&j, &m, (nbt_most_class)kind, &whole));
    n = message(raw, 1, 2, 21, 0x400, part + 20, 5);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(nbt_most_join(&j, &m, (nbt_most_class)kind, &whole) == 1);
    assert(whole.fkt == 0x400 && whole.length == 25 && !memcmp(whole.data, part, 25));
    n = message(raw, 1, 2, 21, 0x401, part + 103, 2);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(nbt_most_join(&j, &m, (nbt_most_class)kind, &whole) == 1 && whole.length == 5 &&
           !memcmp(whole.data, part + 100, 5));
    /* Over the bound: dropped, not truncated. */
    n = message(raw, 1, 2, 19, 0x400, part, NBT_MOST_JOIN_MAX);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(!nbt_most_join(&j, &m, (nbt_most_class)kind, &whole));
    n = message(raw, 1, 2, 21, 0x400, part, 1);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(nbt_most_join(&j, &m, (nbt_most_class)kind, &whole) < 0);
    /* More pending messages than slots: the oldest is replaced. */
    for (i = 0; i <= NBT_MOST_JOIN_SLOTS; ++i) {
        n = message(raw, 1, 2, 19, (uint16_t)(0x500 + i), part, 1);
        kind = nbt_most_parse_any(raw, n, &m);
        assert(!nbt_most_join(&j, &m, (nbt_most_class)kind, &whole));
    }
    n = message(raw, 1, 2, 21, 0x500, part, 1);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(nbt_most_join(&j, &m, (nbt_most_class)kind, &whole) < 0);
    n = message(raw, 1, 2, 21, 0x501, part, 1);
    kind = nbt_most_parse_any(raw, n, &m);
    assert(nbt_most_join(&j, &m, (nbt_most_class)kind, &whole) == 1 && whole.length == 2);
}

static void process_message(void) {
    static const uint8_t expected[] = {7,    0,    0,    0,                /* client */
                                       0,    0,    0,    0,                /* tx type 0 */
                                       1,    0,    0,    0,    0, 0, 0, 0, /* variant {1, 0} */
                                       0x01, 0x01, 0x34, 0x12,             /* dst, src */
                                       0x82, 0x00, 0x00, 0x04, /* FBlock, instance, function */
                                       0x06, 0,    0,    0,    /* op, padding */
                                       0x04, 0x03, 0x02, 0x01, /* tag */
                                       3,    0,    0,    0,    /* length */
                                       0xaa, 0xbb, 0xcc};
    const uint8_t data[3] = {0xaa, 0xbb, 0xcc};
    uint8_t out[64];
    long n = nbt_most_process_message(7, 0x1234, 0x0101, 0x82, 0, 0x400, 6, 0x01020304u, data, 3,
                                      out, sizeof(out));
    assert(n == (long)sizeof(expected) && !memcmp(out, expected, sizeof(expected)));
    assert(nbt_most_process_message(7, 1, 2, 3, 4, 0x1000, 0, 0, data, 3, out, sizeof(out)) < 0);
    assert(nbt_most_process_message(7, 1, 2, 3, 4, 5, 16, 0, data, 3, out, sizeof(out)) < 0);
    assert(nbt_most_process_message(7, 1, 2, 3, 4, 5, 0, 0, data, 3, out, sizeof(expected) - 1) <
           0);
    assert(nbt_most_process_message(7, 1, 2, 3, 4, 5, 0, 0, NULL, 3, out, sizeof(out)) < 0);
}

static void subscriptions(void) {
    nbt_most_policy policy;
    assert(!nbt_most_policy_parse(
        "rx:*.*.*,sub:05.00.621,sub:05.00.228,sub:05.00.626,sub:05.00.CB5,sub:05.00.123", &policy));
    nbt::most::Subscriptions subs;
    subs.configure(policy);
    assert(subs.due(0) < 0);
    nbt_most_wire_entry entry = {0x101, 0x500};
    subs.registry(&entry, 1);
    assert(subs.due(0) == 0);
    unsigned char payload[80];
    assert(subs.prepare(0, 7, 0x1234, 0, payload, sizeof(payload)) == 47);
    const unsigned char expected[] = {1, 0x12, 0x34, 6, 0x21, 2, 0x28, 6, 0x26, 0xc, 0xb5};
    assert(!memcmp(payload + 36, expected, sizeof(expected)));
    assert(payload[16] == 1 && payload[17] == 1 && payload[20] == 5 && payload[22] == 1 &&
           payload[24] == 0);
    assert(subs.due(0) == 1); /* fifth function is a separate batch */
    assert(subs.prepare(1, 7, 0x1234, 0, payload, sizeof(payload)) == 41);
    assert(subs.due(999) < 0);
    assert(subs.due(1000) == 0);
    assert(subs.prepare(0, 7, 0x1234, 1000, payload, sizeof(payload)) > 0);
    assert(subs.due(1000) == 1);
    assert(subs.prepare(1, 7, 0x1234, 1000, payload, sizeof(payload)) > 0);
    assert(subs.due(2999) < 0 && subs.due(3000) == 0);
    for (unsigned i = 0; i < policy.count; ++i) {
        subs.status(0x102, 0x500, (uint16_t)policy.rules[i].fkt);
    }
    assert(subs.due(3000) == 0); /* another node cannot acknowledge */
    for (unsigned i = 1; i < policy.count; ++i) {
        subs.status(0x101, 0x500, (uint16_t)policy.rules[i].fkt);
    }
    assert(subs.due(3000) < 0);
    subs.registry(&entry, 1);
    assert(subs.due(3000) < 0); /* unchanged registry keeps confirmations */
    subs.reset();
    assert(subs.due(3000) == 0);
    subs.registry(NULL, 0);
    assert(subs.due(3000) < 0);
    subs.registry(&entry, 1);
    assert(subs.due(3000) == 0);
    assert(subs.prepare(0, 7, 0x1234, 3000, payload, 4) < 0);
    assert(subs.due(3000) == 0); /* encoding failure leaves it pending */
    assert(subs.prepare(0, 7, 0x1234, 3000, payload, sizeof(payload), true) > 0);
    assert(payload[36] == 3 && subs.due(3000, true) < 0);
    entry.address = 0x5000;
    subs.registry(&entry, 1);
    assert(subs.due(3000) < 0);
    entry.address = 0x203f;
    subs.registry(&entry, 1);
    assert(subs.due(3000) == 0);
    entry.address = 0xffff;
    subs.registry(&entry, 1);
    assert(subs.due(3000) < 0);
}

static void subscription_budget(void) {
    nbt_most_policy policy;
    assert(!nbt_most_policy_parse("rx:05.00.621,sub:05.00.621,tx:82.00.*,tx:A0.03.*", &policy));
    nbt::most::MostSession session;
    assert(session.configure("test", &policy));
    const unsigned char client[] = {7, 0, 0, 0, 0x34, 0x12};
    uint32_t id = 0;
    assert(session.acceptClientReply(client, sizeof(client), &id) == nbt::most::ClientAccepted);
    nbt_most_wire_state state;
    session.updateLink(NBT_MOST_LINK_UP, &state);
    const unsigned char registry[] = {
        4, 0, 0, 0, 1, 1, 0, 5, 2, 1, 0, 0x82, 0x3f, 0x20, 3, 0xa0, 0, 0x50, 1, 0xa0,
    };
    uint8_t record[32];
    assert(session.updateRegistry(registry, sizeof(registry), record, sizeof(record)) > 0);
    const unsigned char *payload = NULL;
    assert(session.prepareSubscription(0, &payload) > 0);
    nbt_most_wire_message message;
    memset(&message, 0, sizeof(message));
    message.fblock = 0xa0;
    message.inst = 3;
    message.fkt = 0xc10;
    message.op = 1;
    assert(nbt_most_wire_encode_message(NBT_MOST_WIRE_SEND, &message, record, sizeof(record)) ==
           24);
    size_t length = 0;
    assert(session.prepareSend(record, 24, 0, &payload, &length) == NBT_MOST_SENT);
    message.inst = 1;
    assert(nbt_most_wire_encode_message(NBT_MOST_WIRE_SEND, &message, record, sizeof(record)) ==
           24);
    assert(session.prepareSend(record, 24, 0, &payload, &length) == NBT_MOST_NOT_ALLOWED);
    memset(&message, 0, sizeof(message));
    message.fblock = 0x82;
    message.fkt = 0x400;
    assert(nbt_most_wire_encode_message(NBT_MOST_WIRE_SEND, &message, record, sizeof(record)) ==
           24);
    for (unsigned i = 0; i < 98; ++i) {
        assert(session.prepareSend(record, 24, 0, &payload, &length) == NBT_MOST_SENT);
    }
    assert(session.prepareSend(record, 24, 0, &payload, &length) == NBT_MOST_BUSY);
    assert(session.prepareSubscription(1000, &payload) > 0);
    for (unsigned i = 0; i < 99; ++i) {
        assert(session.prepareSend(record, 24, 1000, &payload, &length) == NBT_MOST_SENT);
    }
    assert(session.prepareSend(record, 24, 1000, &payload, &length) == NBT_MOST_BUSY);
}

static void wifi_direct_most(void) {
    uint8_t record[NBT_MOST_WIRE_RECORD_MAX];
    size_t len = 0;
    nbt_most_wire_message msg;
    assert(
        nbt::worker::encodeWfdGet(nbt::worker::kFktWlanState3, 11, record, sizeof(record), &len));
    assert(!nbt_most_wire_decode_message(record, len, &msg) && msg.request == 11 &&
           msg.fblock == 0xa0 && msg.inst == 3 && msg.fkt == 0xc10 && msg.op == 1 && !msg.length);
    assert(nbt::worker::encodeWfdSelectState(0x0102u, 1, 12, record, sizeof(record), &len));
    const uint8_t expectedSel[3] = {2, 1, 1};
    assert(!nbt_most_wire_decode_message(record, len, &msg) && msg.request == 12 &&
           msg.fkt == 0xc11 && msg.op == 6 && msg.length == 3 &&
           !memcmp(msg.data, expectedSel, sizeof(expectedSel)));

    memset(&msg, 0, sizeof(msg));
    msg.fblock = 0xa0;
    msg.inst = 3;
    msg.fkt = 0xc10;
    msg.op = nbt::worker::kMostOpStatus;
    const uint8_t stOn[1] = {1};
    msg.data = stOn;
    msg.length = 1;
    nbt::worker::WfdStateReply st;
    assert(nbt::worker::decodeWfdState(msg, &st) && st.state == 1);
    msg.op = nbt::worker::kMostOpError;
    const uint8_t stErr[1] = {0x22};
    msg.data = stErr;
    assert(nbt::worker::decodeWfdState(msg, &st) && st.errorCode == 0x22);
    msg.length = 0;
    assert(!nbt::worker::decodeWfdState(msg, &st));

    // MOST OpTypes of a method's reports (dev66 had these two swapped).
    assert(nbt::worker::kMostOpProcessingAck == 0x0a && nbt::worker::kMostOpResultAck == 0x0d);
    msg.fkt = 0xc11;
    msg.op = nbt::worker::kMostOpResultAck;
    const uint8_t ack3[3] = {5, 0, 1};
    msg.data = ack3;
    msg.length = 3;
    nbt::worker::WfdSelectReply sel;
    assert(nbt::worker::decodeWfdSelect(msg, &sel) && sel.handle == 5 && sel.hasState &&
           sel.state == 1);
    msg.op = nbt::worker::kMostOpProcessingAck;
    msg.length = 2;
    assert(nbt::worker::decodeWfdSelect(msg, &sel) && sel.handle == 5 && !sel.hasState);
    msg.op = nbt::worker::kMostOpErrorAck;
    const uint8_t errAck[3] = {5, 0, 0x0b};
    msg.data = errAck;
    msg.length = 3;
    assert(nbt::worker::decodeWfdSelect(msg, &sel) && sel.errorCode == 0x0b);

    /* UTF-8 WLANConfiguration Status: MAC, DeviceName, IP, Prim, Sec, Display, SSID, Vis, Sec3, Key
     */
    const uint8_t cfgBytes[] = {
        2,   '0', '0', ':', '1', '1', 0,   2,   'B', 'M', 'W', 0,   2,   '1', '7', '2',
        '.', '1', '6', '.', '2', '2', '2', '.', '1', 0,   2,   '0', '-', '0', 0,   2,
        0,   1,   2,   'D', 'I', 'R', 'E', 'C', 'T', '-', '0', '3', '-', 'B', 'M', 'W',
        0,   1,   4,   2,   'S', 'e', 'c', 'r', 'e', 't', '1', '2', 0,
    };
    msg.fkt = 0xc12;
    msg.op = nbt::worker::kMostOpStatus;
    msg.data = cfgBytes;
    msg.length = sizeof(cfgBytes);
    nbt::worker::WfdConfig cfg;
    assert(nbt::worker::decodeWfdConfig(msg, &cfg));
    assert(!strcmp(cfg.ipAddress, "172.16.222.1") && !strcmp(cfg.ssid, "DIRECT-03-BMW") &&
           cfg.hasSsidVisible && cfg.ssidVisible == 1 &&
           !strcmp(cfg.passphrase.data(), "Secret12"));

    /* UTF-16LE SSID & Key, plus hidden SSIDVisible=0 and truncated/control rejections. */
    const uint8_t cfgUtf16[] = {
        2, 0, 2, 0,   2, '1', 0, 2,   0, 2,   0, 0,   0, 'D', 0, 'I', 0, 'R', 0, '1', 0, 0, 0,
        0, 4, 0, 'P', 0, 'a', 0, 's', 0, 's', 0, 'W', 0, 'o', 0, 'r', 0, 'd', 0, '9', 0, 0, 0,
    };
    msg.data = cfgUtf16;
    msg.length = sizeof(cfgUtf16);
    assert(nbt::worker::decodeWfdConfig(msg, &cfg));
    assert(!strcmp(cfg.ssid, "DIR1") && cfg.hasSsidVisible && cfg.ssidVisible == 0 &&
           !strcmp(cfg.passphrase.data(), "PassWord9"));

    msg.data = cfgBytes;
    msg.length = sizeof(cfgBytes) - 2; /* truncated passphrase field */
    assert(!nbt::worker::decodeWfdConfig(msg, &cfg));
    assert(!nbt::worker::validWfdSsid("BAD\tSSID") && !nbt::worker::validWfdSsid(""));
    assert(!nbt::worker::validWfdPassphrase("short", 5) &&
           !nbt::worker::validWfdPassphrase("bad\npass!", 9));
}

/* NBT-AAD's Wi-Fi choice: NBT_ALT_WIFI among the first 15 settings wins,
 * else its "Use WiFi Direct Network" flag file. */
static void android_auto_wifi(void) {
    using nbt::worker::aadWifiSetting;
    using nbt::worker::kAadCarplayHotspot;
    using nbt::worker::kAadWifiDirect;
    struct Case {
        const char *env;
        bool flag;
        nbt::worker::AadWifi want;
    } cases[] = {
        {"", false, kAadCarplayHotspot},
        {"", true, kAadWifiDirect},
        {"NBT_ALT_WIFI=DIRECT\n", false, kAadWifiDirect},
        {"NBT_ALT_WIFI= direct \r\n", false, kAadWifiDirect},
        {"NBT_ALT_WIFI=wfd", false, kAadWifiDirect},
        {"NBT_ALT_WIFI=P2P\n", false, kAadWifiDirect},
        {"NBT_ALT_WIFI=CARPLAY\n", true, kAadCarplayHotspot},
        {"NBT_ALT_WIFI=BOTH\n", true, kAadCarplayHotspot},
        {"NBT_ALT_WIFI=\nNBT_ALT_WIFI=CARPLAY\n", true, kAadWifiDirect},
        {"# NBT_ALT_WIFI=DIRECT\nNBT_X=1\n", false, kAadCarplayHotspot},
        {" NBT_ALT_WIFI=DIRECT\n", false, kAadCarplayHotspot},
        {"NBT_ALT_WIFI=DIRECTLY\n", false, kAadCarplayHotspot},
        {"A=1\nB=2\nC=3\nD=4\nE=5\nF=6\nG=7\nH=8\nI=9\nJ=10\nK=11\nL=12\nM=13\nN=14\n"
         "NBT_ALT_WIFI=DIRECT\n",
         false, kAadWifiDirect},
        {"A=1\nB=2\nC=3\nD=4\nE=5\nF=6\nG=7\nH=8\nI=9\nJ=10\nK=11\nL=12\nM=13\nN=14\nO=15\n"
         "NBT_ALT_WIFI=DIRECT\n",
         false, kAadCarplayHotspot},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(aadWifiSetting(cases[i].env, strlen(cases[i].env), cases[i].flag) == cases[i].want);
    }
    assert(aadWifiSetting(NULL, 0, true) == kAadWifiDirect);
}

/* dhcpd's leases file: one "lease " block per address handed out. */
static void dhcp_leases(void) {
    const char text[] = "# comment lease 1\nlease 172.16.222.100 {\n  starts 4 2000/01/01;\n}\n"
                        "lease 172.16.222.101 {\n  binding state active;\n}\n leases\n";
    assert(nbt::worker::countDhcpLeases(text, sizeof(text) - 1) == 2);
    assert(nbt::worker::countDhcpLeases("", 0) == 0);
    assert(nbt::worker::countDhcpLeases("lease ", 6) == 1 &&
           nbt::worker::countDhcpLeases("lease", 5) == 0);
}

static void volume_level(void) {
    nbt::most::VolumeLevel vol;
    int pct = -1;
    const uint8_t ok[5] = {0x01, 0x00, 25, 0x00, 100};
    const uint8_t ok2[5] = {0x01, 0x00, 30, 0x00, 100};
    const uint8_t scaled[5] = {0x01, 0x00, 16, 0x00, 32};
    const uint8_t bad_max[5] = {0x01, 0x00, 10, 0x00, 0};
    const uint8_t over[5] = {0x01, 0x00, 101, 0x00, 100};
    assert(!vol.feed(0x21, 0xc08, 0x0c, ok, 5, 100, pct));
    assert(!vol.feed(0x20, 0x200, 0x0c, ok, 5, 100, pct));
    assert(!vol.feed(0x20, 0xc08, 0x01, ok, 5, 100, pct));
    assert(!vol.feed(0x20, 0xc08, 0x0c, ok, 4, 100, pct));
    assert(!vol.feed(0x20, 0xc08, 0x0c, bad_max, 5, 100, pct));
    assert(!vol.feed(0x20, 0xc08, 0x0c, over, 5, 100, pct));
    assert(vol.feed(0x20, 0xc08, 0x0c, ok, 5, 100, pct) && pct == 25);
    assert(!vol.feed(0x20, 0xc08, 0x0c, ok, 5, 120, pct));
    assert(vol.feed(0x20, 0xc08, 0x0c, ok, 5, 150, pct) && pct == 25);
    assert(vol.feed(0x20, 0xc08, 0x0c, ok2, 5, 155, pct) && pct == 30);
    assert(vol.feed(0x20, 0xc08, 0x0c, scaled, 5, 200, pct) && pct == 50);
    vol.reset();
    assert(vol.feed(0x20, 0xc08, 0x0c, scaled, 5, 205, pct) && pct == 50);
}

int main(void) {
    policy();
    wire();
    joining();
    process_message();
    subscriptions();
    subscription_budget();
    wifi_direct_most();
    android_auto_wifi();
    dhcp_leases();
    volume_level();
    puts("most checks passed");
    return 0;
}
