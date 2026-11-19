/* Host LD_PRELOAD harness exercising real preload interposition, caching,
 * clock expiry, reconnect reuse, concurrent initialization, and active suppression. */
#include "nbt/capture/control.h"
#include "nbt/capture/diag.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void DSIClientDataResponse(void *client, void *stream);
extern uint32_t nbt_fake_dsi_base_count(void);
extern void (*g_reenter_hook)(void *client, void *stream);

static size_t make_rotary_stream(unsigned char *buf, nbt_dsi_c_stream *s) {
    memset(buf, 0, 64);
    buf[8] = 0x01;
    buf[11] = 0x80; /* 0x80000001 */
    unsigned char *m = buf + 16;
    m[4] = 2;
    m[8] = 1;
    m[16] = 0xE5;
    m[17] = 0x03;
    m[18] = 0x46;
    m[19] = 0x0C;
    m[20] = 0x0C;
    m[28] = 5;
    m[32 + 2] = 1;
    memset(s, 0, sizeof(*s));
    s->capacity = 64;
    s->populated = 53;
    s->read_pos = 0;
    s->buffer = buf;
    return 53;
}

static size_t make_hard_back_stream(unsigned char *buf, nbt_dsi_c_stream *s, uint8_t held) {
    memset(buf, 0, 64);
    buf[8] = 0x01;
    buf[11] = 0x80;
    unsigned char *m = buf + 16;
    m[4] = 2;
    m[8] = 1;
    m[16] = 0xE5;
    m[17] = 0x03;
    m[18] = 0x47;
    m[19] = 0x0C;
    m[20] = 0x0C;
    m[28] = 7;
    m[32 + 1] = held;
    memset(s, 0, sizeof(*s));
    s->capacity = 64;
    s->populated = 55;
    s->read_pos = 0;
    s->buffer = buf;
    return 55;
}

static void write_clock(const char *path, uint64_t ms) {
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%llu\n", (unsigned long long)ms);
        fclose(f);
    }
}

static void write_live(const char *path, pid_t pid, uint64_t ms) {
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%ld %llu\n", (long)pid, (unsigned long long)ms);
        fclose(f);
    }
}

struct worker_arg {
    pthread_barrier_t *bar;
    unsigned char *client;
};

static void *concurrent_worker(void *arg) {
    struct worker_arg *w = (struct worker_arg *)arg;
    unsigned char buf[64];
    nbt_dsi_c_stream s;
    make_rotary_stream(buf, &s);
    pthread_barrier_wait(w->bar);
    for (int i = 0; i < 4; ++i) {
        DSIClientDataResponse(w->client, &s);
    }
    return NULL;
}

int main(int argc, char **argv) {
    const int expect_active = argc > 1 && strcmp(argv[1], "active") == 0;
    const char *ctrl_path = getenv("NBT_TEST_CAPTURE_CONTROL");
    const char *live_path = getenv("NBT_TEST_GUI_LIVENESS");
    const char *clock_path = getenv("NBT_TEST_CAPTURE_CLOCK_FILE");
    const char *diag_path = getenv("NBT_TEST_CAPTURE_DIAG");
    if (!ctrl_path || !live_path || !clock_path || !diag_path) {
        return 2;
    }
    write_clock(clock_path, 1000);
    nbt_capture_control ctrl;
    nbt_capture_control_init(&ctrl, getpid(), 11u, 2u, NULL);
    ctrl.state = NBT_CAPTURE_ON;
    if (nbt_capture_control_write(ctrl_path, &ctrl) != 0) {
        return 3;
    }
    write_live(live_path, getpid(), 1000);

    /* Concurrent initialization across 6 threads with MOSTSubSystem.DSYSMOSTServer client. */
    unsigned char client_most[0xE0];
    memset(client_most, 0, sizeof(client_most));
    memcpy(client_most + 0x20, "MOSTSubSystem.DSYSMOSTServer", 29);

    pthread_t th[6];
    pthread_barrier_t bar;
    pthread_barrier_init(&bar, NULL, 6);
    struct worker_arg warg = {&bar, client_most};
    for (int i = 0; i < 6; ++i) {
        pthread_create(&th[i], NULL, concurrent_worker, &warg);
    }
    for (int i = 0; i < 6; ++i) {
        pthread_join(th[i], NULL);
    }
    pthread_barrier_destroy(&bar);

    /* Verify non-destructive stream cursor: forward count depends on observer vs active. */
    uint32_t expected = expect_active ? 0u : 24u;
    if (nbt_fake_dsi_base_count() != expected) {
        fprintf(stderr, "concurrent forward count=%u expected=%u\n", nbt_fake_dsi_base_count(),
                expected);
        return 4;
    }

    /* Other client + freed/reused client must forward unchanged. */
    unsigned char raw[64];
    nbt_dsi_c_stream s;
    make_rotary_stream(raw, &s);
    memcpy(client_most + 0x20, "OtherService.Server", 20);
    DSIClientDataResponse(client_most, &s);
    if (s.read_pos != 0 || nbt_fake_dsi_base_count() != expected + 1u) {
        return 5;
    }
    /* Reconnect back to MOSTSubSystem.DSYSMOSTServer on same struct resumes classification. */
    memcpy(client_most + 0x20, "MOSTSubSystem.DSYSMOSTServer", 29);

    /* Heartbeat expiry within the 100ms cache window must fail open immediately at 2500ms! */
    write_clock(clock_path, 2450); /* live=1000 -> age=1450 < 1500, cached read at 2450 */
    DSIClientDataResponse(client_most, &s);
    if (!expect_active && nbt_fake_dsi_base_count() != expected + 2u) {
        return 6;
    }
    if (expect_active && nbt_fake_dsi_base_count() != expected + 1u) {
        return 7;
    }
    uint32_t before_expire = nbt_fake_dsi_base_count();
    write_clock(clock_path,
                2500); /* only +50ms later (<100ms cache!), age=1500 >= 1500 -> EXPIRED */
    DSIClientDataResponse(client_most, &s);
    if (nbt_fake_dsi_base_count() != before_expire + 1u) {
        fprintf(stderr, "expiry failed to forward within cache window\n");
        return 8;
    }

    /* Fresh heartbeat at 2600ms + 5s escape test (press at 2600, release at 7600 -> escape!). */
    write_clock(clock_path, 2600);
    write_live(live_path, getpid(), 2600);
    nbt_dsi_c_stream bs;
    unsigned char bbuf[64];
    make_hard_back_stream(bbuf, &bs, 1);
    uint32_t before_back = nbt_fake_dsi_base_count();
    DSIClientDataResponse(client_most, &bs);
    write_clock(clock_path, 7600);
    write_live(live_path, getpid(), 7600);
    make_hard_back_stream(bbuf, &bs, 0);
    DSIClientDataResponse(client_most, &bs);
    /* Active mode drops the release that crossed 5s AND latches generation=2 off; observer forwards
     * both. */
    if (!expect_active && nbt_fake_dsi_base_count() != before_back + 2u) {
        return 9;
    }
    if (expect_active && nbt_fake_dsi_base_count() != before_back) {
        return 10;
    }
    /* Next input with SAME generation=2 must forward even in active mode! */
    write_clock(clock_path, 7750);
    write_live(live_path, getpid(), 7750);
    DSIClientDataResponse(client_most, &s);
    if (nbt_fake_dsi_base_count() != (expect_active ? before_back + 1u : before_back + 3u)) {
        return 11;
    }
    /* Nested reentrant callback must tail-forward without deadlock/suppression. */
    uint32_t before_nest = nbt_fake_dsi_base_count();
    g_reenter_hook = DSIClientDataResponse;
    memcpy(client_most + 0x20, "OtherService.Server", 20);
    DSIClientDataResponse(client_most, &s);
    if (nbt_fake_dsi_base_count() != before_nest + 2u) {
        return 12;
    }

    nbt_capture_diag_shm snap;
    if (nbt_capture_diag_read(diag_path, &snap) != 0 ||
        snap.observer_mode != (expect_active ? 0u : 1u) || snap.write_seq < 3u) {
        return 13;
    }
    puts("capture-shim-test: OK");
    return 0;
}
