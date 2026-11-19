// SPDX-License-Identifier: MPL-2.0
/* Preloaded DSIClientDataResponse shim: zero heap/static init. The car build is
 * active (-DNBT_CAPTURE_OBSERVER=0); the observer default only forwards. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "nbt/capture/classifier.h"
#include "nbt/capture/control.h"
#include "shim_diag.h"
#include "shim_env.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef NBT_CAPTURE_OBSERVER
#define NBT_CAPTURE_OBSERVER 1
#endif

typedef void (*dsi_data_response_fn)(void *, void *);
void DSIClientDataResponse(void *client, void *stream);

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t g_reentry_key;
static int g_reentry_ready = 0;

static dsi_data_response_fn g_real_fn = NULL;
static int g_process_ok = 0;

static int g_has_read = 0;
static uint64_t g_last_read_ms = 0;
static int g_cached_ok = 0;
static nbt_capture_control g_cached_ctrl;
static uint64_t g_cached_live_ts = 0;
static nbt_capture_escape_state g_escape;

static dsi_data_response_fn resolve_real_dsi(void) {
    const char *base_lib = NBT_CAPTURE_DSI_BASE_LIB;
    void *sym = dlsym(RTLD_NEXT, "DSIClientDataResponse");
    void *handle = NULL;
    if (sym && sym != (void *)&DSIClientDataResponse) {
        return (dsi_data_response_fn)sym;
    }
#ifdef NBT_TESTING
    {
        const char *override_lib = getenv("NBT_TEST_DSI_BASE_LIB");
        if (override_lib && override_lib[0]) {
            base_lib = override_lib;
        }
    }
#endif
    handle = dlopen(base_lib, RTLD_NOLOAD | RTLD_LAZY);
    if (handle) {
        sym = dlsym(handle, "DSIClientDataResponse");
        if (sym && sym != (void *)&DSIClientDataResponse) {
            return (dsi_data_response_fn)sym;
        }
    }
    return NULL;
}

static void init_shim(void) {
    nbt_capture_escape_init(&g_escape);
    if (pthread_key_create(&g_reentry_key, NULL) == 0) {
        g_reentry_ready = 1;
    }
    g_real_fn = resolve_real_dsi();
    g_process_ok = nbt_shim_is_hmi_process();
    if (g_process_ok) {
        (void)nbt_shim_diag_open(NBT_CAPTURE_OBSERVER);
        if (!g_real_fn) {
            /* Nothing can be forwarded; say so instead of failing silently. */
            pthread_mutex_lock(&g_state_lock);
            nbt_shim_diag_record(NBT_CAPTURE_DIAG_UNRESOLVED, 0, 0, 0, 0, nbt_shim_monotonic_ms(),
                                 0, 0, 0);
            pthread_mutex_unlock(&g_state_lock);
        }
    }
}

__attribute__((visibility("default"))) int nbt_capture_selftest_begin(const char *dir,
                                                                      dsi_data_response_fn forward,
                                                                      uint64_t (*clock)(void),
                                                                      int *observer) {
    (void)pthread_once(&g_once, init_shim);
    if (g_process_ok || !forward || nbt_shim_is_hmi_process() ||
        nbt_shim_selftest_set(dir, clock)) {
        return -1;
    }
    pthread_mutex_lock(&g_state_lock);
    g_real_fn = forward;
    g_has_read = 0;
    g_cached_ok = 0;
    nbt_capture_escape_init(&g_escape);
    (void)nbt_shim_diag_open(NBT_CAPTURE_OBSERVER);
    pthread_mutex_unlock(&g_state_lock);
    g_process_ok = 1;
    if (observer) {
        *observer = NBT_CAPTURE_OBSERVER ? 1 : 0;
    }
    return nbt_shim_diag_ready() ? 0 : -1;
}

/* The GUI that wrote the record still exists (a dead GUI never holds capture). */
static int gui_alive(int32_t pid) {
    return pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM);
}

static int client_is_most_server(const void *client) {
    static const char kMost[] = "MOSTSubSystem.DSYSMOSTServer";
    const unsigned char *p = (const unsigned char *)client;
    return client != NULL && memcmp(p + 0x20u, kMost, sizeof(kMost)) == 0;
}

static int liveness_fresh(uint64_t live_ts, uint32_t watchdog_ms, uint64_t now_ms) {
    if (live_ts > now_ms + 5000u) {
        return 0;
    }
    if (now_ms <= live_ts) {
        return 1;
    }
    return (now_ms - live_ts) < (uint64_t)watchdog_ms;
}

/* Controller and favourite-key status messages: the only ones capture may hold. */
static int is_input_message(const nbt_capture_packet *p) {
    if (!p->has_most || p->op != 0x0Cu) {
        return 0;
    }
    if (p->fblock == NBT_CAPTURE_FBLOCK_CENTER_STACK) {
        return p->fkt == NBT_CAPTURE_FKT_FAVOURITES;
    }
    return p->fblock == 0xE5u && (p->fkt == 0x0C46u || p->fkt == 0x0C47u || p->fkt == 0x0C4Fu ||
                                  p->fkt == 0x0C10u || p->fkt == 0x0C20u);
}

static int evaluate_call(const nbt_dsi_c_stream *stream, uint64_t now_ms, nbt_capture_decision *dec,
                         uint64_t *gen_out, uint8_t *diag_kind, nbt_capture_packet *pkt_out) {
    nbt_capture_control ctrl;
    int authorized = 0;
    *dec = NBT_CAP_DECISION_FORWARD;
    *gen_out = 0;
    *diag_kind = 0;
    memset(pkt_out, 0, sizeof(*pkt_out));

    pthread_mutex_lock(&g_state_lock);
    if (!g_has_read || now_ms < g_last_read_ms || now_ms - g_last_read_ms >= NBT_CAPTURE_CACHE_MS) {
        nbt_capture_control fresh;
        uint64_t live_ts = 0;
        g_has_read = 1;
        g_last_read_ms = now_ms;
        if (nbt_capture_control_read(nbt_shim_control_path(), &fresh) == 0 &&
            fresh.state == NBT_CAPTURE_ON &&
            nbt_capture_liveness_read(nbt_shim_liveness_path(), (pid_t)fresh.gui_pid, &live_ts) ==
                0 &&
            liveness_fresh(live_ts, fresh.watchdog_ms, now_ms) && gui_alive(fresh.gui_pid)) {
            g_cached_ctrl = fresh;
            g_cached_live_ts = live_ts;
            g_cached_ok = 1;
        } else {
            g_cached_ok = 0;
        }
    }
    if (g_cached_ok) {
        if (!liveness_fresh(g_cached_live_ts, g_cached_ctrl.watchdog_ms, now_ms)) {
            g_cached_ok = 0; /* expired within cache window: never retain authorization */
            *diag_kind = NBT_CAPTURE_DIAG_EXPIRED;
            *gen_out = g_cached_ctrl.generation;
        } else {
            ctrl = g_cached_ctrl;
            authorized = 1;
        }
    }
    if (!authorized && g_escape.hard_back_held) {
        /* Capture is off: a BACK hold cannot go on counting toward the escape,
         * since its release is not seen here. */
        nbt_shim_diag_record(
            NBT_CAPTURE_DIAG_HOLD_DROPPED, 0, 0, 0, 0, now_ms, g_escape.back_generation, 0,
            nbt_shim_tenths(now_ms >= g_escape.back_press_ms ? now_ms - g_escape.back_press_ms
                                                             : 0));
        g_escape.hard_back_held = 0;
        g_escape.back_press_ms = 0;
    }
    if (authorized) {
        (void)nbt_capture_inspect_stream(stream, &ctrl, pkt_out);
        if (pkt_out->is_candidate) {
            *dec = nbt_capture_evaluate(pkt_out, &ctrl, 1, NBT_CAPTURE_OBSERVER ? 1 : 0, now_ms,
                                        &g_escape);
            *gen_out = ctrl.generation;
            if (g_escape.dropped) {
                nbt_shim_diag_record(NBT_CAPTURE_DIAG_HOLD_DROPPED, 0, 0, 0, 0, now_ms,
                                     g_escape.dropped_generation, 1u,
                                     nbt_shim_tenths(g_escape.held_ms));
                g_escape.dropped = 0;
            }
            if (*dec == NBT_CAP_DECISION_SUPPRESS) {
                *diag_kind = NBT_CAPTURE_DIAG_SUPPRESS;
            } else if (*dec == NBT_CAP_DECISION_ESCAPE) {
                g_cached_ok = 0;
                *diag_kind = NBT_CAPTURE_DIAG_ESCAPE;
            } else {
                *diag_kind = NBT_CAPTURE_DIAG_OBSERVE;
            }
        }
    }
    if (*diag_kind != 0) {
        nbt_shim_diag_record(
            *diag_kind, pkt_out->fblock, pkt_out->fkt, pkt_out->inst, pkt_out->op, now_ms, *gen_out,
            authorized ? 1u : 0u,
            *diag_kind == NBT_CAPTURE_DIAG_ESCAPE ? nbt_shim_tenths(g_escape.held_ms) : 0);
    }
    pthread_mutex_unlock(&g_state_lock);
    return authorized;
}

__attribute__((visibility("default"))) void DSIClientDataResponse(void *client, void *stream) {
    nbt_capture_packet pre, pkt;
    nbt_capture_decision dec = NBT_CAP_DECISION_FORWARD;
    uint64_t gen = 0;
    uint8_t diag_kind = 0;
    uint64_t now_ms;

    (void)pthread_once(&g_once, init_shim);
    if (!g_real_fn) {
        return;
    }
    if (!g_process_ok || !g_reentry_ready || !client || !stream ||
        pthread_getspecific(g_reentry_key) != NULL || !client_is_most_server(client)) {
        g_real_fn(client, stream);
        return;
    }
    (void)pthread_setspecific(g_reentry_key, (void *)1);
    if (nbt_capture_inspect_stream((const nbt_dsi_c_stream *)stream, NULL, &pre) == 0 &&
        is_input_message(&pre)) {
        now_ms = nbt_shim_monotonic_ms();
        (void)evaluate_call((const nbt_dsi_c_stream *)stream, now_ms, &dec, &gen, &diag_kind, &pkt);
    }

#if NBT_CAPTURE_OBSERVER
    (void)dec;
    g_real_fn(client, stream);
#else
    if (dec == NBT_CAP_DECISION_FORWARD) {
        g_real_fn(client, stream);
    }
#endif
    (void)pthread_setspecific(g_reentry_key, NULL);
}
