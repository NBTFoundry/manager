// SPDX-License-Identifier: MPL-2.0
/* Host stand-in for libsys_dsi_colibry: exports the mangled C++ symbols used
 * by src/most_input.c as C functions, records calls to NBT_FAKE_DSI_LOG and
 * replays the DSI payloads (hex, one per line) in NBT_FAKE_DSI_SCRIPT. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_PARTY 0x110
#define SERVER_PARTY 0x118
#define CLIENT_CHANNEL 0x130
#define CLIENT_SOCKET 0x138
#define CLIENT_IPC_FD 0x140

/* Offset-to-top, type info and the 8 virtual slots of CDSIClient in
 * NBTevo_Y21432J (docs/head-unit-interfaces.md); exactly that size, so that
 * the sanitizer build reports any read past the table. */
void *_ZTV10CDSIClient[2 + 8];

static unsigned char *client;
static unsigned request;
static unsigned char payload[512];
static size_t payload_length;
static volatile int seen_register, seen_controls;

struct rule {
    char match[128];
    unsigned id;
    unsigned char data[1024];
    uint32_t length;
    int once;
};
struct queued {
    unsigned id;
    unsigned char data[1024];
    uint32_t length;
};
static pthread_mutex_t rule_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rule rules[32];
static size_t rule_count;
static struct queued pending[32];
static size_t pending_count;

static void note(const char *format, unsigned long a, unsigned long b) {
    const char *path = getenv("NBT_FAKE_DSI_LOG");
    FILE *log = path ? fopen(path, "a") : NULL;
    if (!log) {
        return;
    }
    fprintf(log, format, a, b);
    fputc('\n', log);
    fclose(log);
}

static void pause_ms(long ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

void _ZN14CDSIConnectionC1Ev(void *connection) {
    (void)connection;
    note("CONNECTION", 0, 0);
}

int _ZN14CDSIConnection3addER10CDSIClient(void *connection, void *c) {
    (void)connection;
    note("ADD %lu", c == client, 0);
    return 1;
}

void _ZN10CDSIClientC1EPKcS1_ii(void *c, const char *interface, const char *role, int major,
                                int minor) {
    const int fd = 42;
    void **vptr = _ZTV10CDSIClient + 2;
    client = c;
    memcpy(client, &vptr, sizeof(vptr));
    memcpy(client + CLIENT_IPC_FD, &fd, sizeof(fd));
    note(!strcmp(interface, "DSYSMOSTServer") && !strcmp(role, "MOSTSubSystem")
             ? "CLIENT %lu.%lu"
             : "CLIENT-BAD %lu.%lu",
         (unsigned long)major, (unsigned long)minor);
}

void _ZN10CDSIClient15setNotificationEj(void *c, unsigned attribute) {
    note(c == client ? "NOTIFY %lx" : "NOTIFY-BAD %lx", attribute, 0);
}

void _ZN11CDSIOStreamC1ER11CDSIChannelP13CDSITCPSocketiN3DSI11RequestTypeENS4_7CommandEjRK8SPartyIDS9_i(
    void *stream, void *channel, void *socket, int fd, unsigned type, unsigned command, unsigned id,
    const void *from, const void *to, int flags) {
    int ok = channel == client + CLIENT_CHANNEL && socket == client + CLIENT_SOCKET && fd == 42 &&
             type == 0x100 && command == 7 && from == client + CLIENT_PARTY &&
             to == client + SERVER_PARTY && !flags;
    (void)stream;
    request = ok ? id : 0xdead;
    payload_length = 0;
}

void _ZN11CDSIOStream5writeEPKvj(void *stream, const void *data, unsigned length) {
    (void)stream;
    if (length <= sizeof(payload) - payload_length) {
        memcpy(payload + payload_length, data, length);
        payload_length += length;
    }
}

int _ZN11CDSIOStream4sendEi(void *stream, int timeout) {
    char hex[2 * sizeof(payload) + 1];
    size_t i;
    const char *path = getenv("NBT_FAKE_DSI_LOG");
    FILE *log = path ? fopen(path, "a") : NULL;
    (void)stream;
    (void)timeout;
    for (i = 0; i < payload_length; ++i) {
        snprintf(hex + 2 * i, 3, "%02x", payload[i]);
    }
    hex[2 * payload_length] = 0;
    if (log) {
        fprintf(log, "REQUEST %x %s\n", request, hex);
        fclose(log);
    }
    if (request == 0x2) {
        size_t i;
        pthread_mutex_lock(&rule_lock);
        for (i = 0; i < rule_count; ++i) {
            if (rules[i].match[0] && strstr(hex, rules[i].match) &&
                pending_count < sizeof(pending) / sizeof(pending[0])) {
                struct queued *q = &pending[pending_count++];
                q->id = rules[i].id;
                q->length = rules[i].length;
                memcpy(q->data, rules[i].data, rules[i].length);
                if (rules[i].once) {
                    rules[i].match[0] = 0;
                }
            }
        }
        pthread_mutex_unlock(&rule_lock);
    }
    if (request == 0xf) {
        seen_register = 1;
    }
    if (request == 0x1) {
        seen_controls = 1;
    }
    if (request == 0x5) {
        note("RELEASED", 0, 0);
    }
    return 0;
}

void _ZN11CDSIOStreamD1Ev(void *stream) {
    (void)stream;
}

/* The client reads id, payload length and payload pointer at these offsets. */
static void deliver(uint32_t id, const unsigned char *data, uint32_t length) {
    unsigned char stream[0x60];
    void (*on_data)(void *, void *);
    void **vtable;
    memset(stream, 0, sizeof(stream));
    memcpy(stream + 0x08, &length, sizeof(length));
    memcpy(stream + 0x10, &data, sizeof(data));
    memcpy(stream + 0x48, &id, sizeof(id));
    memcpy(&vtable, client, sizeof(vtable));
    *(void **)&on_data = vtable[7];
    on_data(client, stream);
}

static void call_slot(unsigned slot) {
    void (*function)(void *);
    void **vtable;
    memcpy(&vtable, client, sizeof(vtable));
    *(void **)&function = vtable[slot];
    function(client);
}

void _ZN14CDSIConnection5startEv(void *connection) {
    const unsigned char refused[6] = {0xff, 0xff, 0xff, 0xff, 0, 0};
    const unsigned char accepted[6] = {7, 0, 0, 0, 0x34, 0x12};
    const char *script = getenv("NBT_FAKE_DSI_SCRIPT");
    char line[4200];
    FILE *in;
    int i;
    (void)connection;
    call_slot(3);
    for (i = 0; i < 200 && !seen_register; ++i) {
        pause_ms(10);
    }
    deliver(0x80000000u, refused, sizeof(refused) - 1); /* too short: ignored */
    if (getenv("NBT_FAKE_DSI_REFUSE")) {
        deliver(0x80000000u, refused, sizeof(refused));
    }
    deliver(0x80000000u, accepted, sizeof(accepted));
    if (!getenv("NBT_FAKE_DSI_NO_CONTROLS")) {
        for (i = 0; i < 200 && !seen_controls; ++i) {
            pause_ms(10);
        }
    }
    deliver(0x80000001u, NULL, 40); /* no payload: ignored */
    in = script ? fopen(script, "r") : NULL;
    while (in && fgets(line, sizeof(line), in)) {
        unsigned char data[2048];
        size_t n = 0;
        unsigned value, id = 0x80000001u;
        const char *hex = line;
        if (!strncmp(line, "WAIT ", 5)) {
            const long ms = strtol(line + 5, NULL, 10);
            if (ms > 0 && ms <= 5000) {
                pause_ms(ms);
            }
            continue;
        }
        if (!strncmp(line, "DETACH", 6)) {
            call_slot(4);
            call_slot(3);
            continue;
        }
        if (!strncmp(line, "ON ", 3) || !strncmp(line, "ON1 ", 4)) {
            int once = !strncmp(line, "ON1 ", 4);
            const char *p = line + (once ? 4 : 3);
            const char *sp = strchr(p, ' ');
            if (sp && rule_count < sizeof(rules) / sizeof(rules[0])) {
                struct rule *r = &rules[rule_count];
                size_t mlen = (size_t)(sp - p);
                if (mlen > 0 && mlen < sizeof(r->match)) {
                    memcpy(r->match, p, mlen);
                    r->match[mlen] = 0;
                    r->id = 0x80000001u;
                    r->once = once;
                    p = sp + 1;
                    if (!strncmp(p, "ID ", 3)) {
                        char *end;
                        r->id = (unsigned)strtoul(p + 3, &end, 16);
                        p = end + (*end == ' ');
                    }
                    r->length = 0;
                    while (r->length < sizeof(r->data) &&
                           sscanf(p + 2 * r->length, "%2x", &value) == 1) {
                        r->data[r->length++] = (unsigned char)value;
                    }
                    pthread_mutex_lock(&rule_lock);
                    ++rule_count;
                    pthread_mutex_unlock(&rule_lock);
                }
            }
            continue;
        }
        /* "ID <hex id> <hex payload>" delivers another information or attribute. */
        if (!strncmp(line, "ID ", 3)) {
            char *end;
            id = (unsigned)strtoul(line + 3, &end, 16);
            hex = end + (*end == ' ');
        }
        while (n < sizeof(data) && sscanf(hex + 2 * n, "%2x", &value) == 1) {
            data[n++] = (unsigned char)value;
        }
        deliver(id, data, (uint32_t)n);
        pause_ms(2);
    }
    if (in) {
        fclose(in);
    }
    note("DONE", 0, 0);
    /* Ongoing traffic: drain queued request replies cleanly, plus idle tick. */
    for (;;) {
        struct queued batch[32];
        size_t count = 0, j;
        pthread_mutex_lock(&rule_lock);
        if (pending_count > 0) {
            count = pending_count;
            memcpy(batch, pending, count * sizeof(batch[0]));
            pending_count = 0;
        }
        pthread_mutex_unlock(&rule_lock);
        for (j = 0; j < count; ++j) {
            deliver(batch[j].id, batch[j].data, batch[j].length);
        }
        const unsigned char tick[4] = {0x01, 0xe5, 0x03, 0x00};
        deliver(0x80000002u, tick, sizeof(tick));
        pause_ms(10);
    }
}
