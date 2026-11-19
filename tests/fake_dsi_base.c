/* Mock libsys_dsi_base.gpp.so.4 exporting C DSIClientDataResponse for LD_PRELOAD tests. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_forward_count = 0;

void (*g_reenter_hook)(void *client, void *stream) = NULL;

__attribute__((visibility("default"))) void nbt_fake_dsi_base_reset(void) {
    pthread_mutex_lock(&g_log_lock);
    g_forward_count = 0;
    pthread_mutex_unlock(&g_log_lock);
}

__attribute__((visibility("default"))) uint32_t nbt_fake_dsi_base_count(void) {
    uint32_t v;
    pthread_mutex_lock(&g_log_lock);
    v = g_forward_count;
    pthread_mutex_unlock(&g_log_lock);
    return v;
}

__attribute__((visibility("default"))) void DSIClientDataResponse(void *client, void *stream) {
    const char *path = getenv("NBT_FAKE_DSI_BASE_LOG");
    const char *name = client ? ((const char *)client + 0x20) : "(null)";
    pthread_mutex_lock(&g_log_lock);
    ++g_forward_count;
    if (path && path[0]) {
        FILE *f = fopen(path, "a");
        if (f) {
            fprintf(f, "FORWARD name=%s stream=%p\n", name, stream);
            fclose(f);
        }
    }
    pthread_mutex_unlock(&g_log_lock);
    if (g_reenter_hook) {
        void (*hook)(void *, void *) = g_reenter_hook;
        g_reenter_hook = NULL;
        hook(client, stream);
    }
}
