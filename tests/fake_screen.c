#include "nbt/qnx_screen.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SLOTS 4

typedef struct {
    int used;
    int has_window;
    int has_buffer;
    int visible;
    int posted;
    int width;
    int height;
    int format;
    char id[33];
    unsigned char pixels[1440 * 540 * 4];
} FakeScreenSlot;

static FakeScreenSlot slots[MAX_SLOTS];

static void trace(const char *event) {
    const char *path = getenv("NBT_FAKE_SCREEN_LOG");
    FILE *file;
    if (!path || !(file = fopen(path, "a"))) {
        return;
    }
    fprintf(file, "%s\n", event);
    fclose(file);
}

static FakeScreenSlot *slotFromContext(nbt_screen_context c) {
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (slots[i].used && c == (nbt_screen_context)&slots[i].used) {
            return &slots[i];
        }
    }
    return NULL;
}

static FakeScreenSlot *slotFromWindow(nbt_screen_window w) {
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (slots[i].used && slots[i].has_window && w == (nbt_screen_window)&slots[i].has_window) {
            return &slots[i];
        }
    }
    return NULL;
}

static FakeScreenSlot *slotFromBuffer(nbt_screen_buffer b) {
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (slots[i].used && slots[i].has_buffer && b == (nbt_screen_buffer)&slots[i].has_buffer) {
            return &slots[i];
        }
    }
    return NULL;
}

int screen_create_context(nbt_screen_context *p, int flags) {
    if (!p || flags != NBT_SCREEN_APPLICATION_CONTEXT) {
        return -1;
    }
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (!slots[i].used) {
            memset(&slots[i], 0, sizeof(slots[i]));
            slots[i].used = 1;
            slots[i].width = 640;
            slots[i].height = 240;
            *p = (nbt_screen_context)&slots[i].used;
            return 0;
        }
    }
    return -1;
}

int screen_destroy_context(nbt_screen_context p) {
    FakeScreenSlot *s = slotFromContext(p);
    trace("destroy-context");
    if (!s) {
        return -1;
    }
    s->used = 0;
    return 0;
}

int screen_create_window(nbt_screen_window *p, nbt_screen_context c) {
    FakeScreenSlot *s = slotFromContext(c);
    if (!p || !s || s->has_window) {
        return -1;
    }
    s->has_window = 1;
    *p = (nbt_screen_window)&s->has_window;
    return 0;
}

int screen_destroy_window(nbt_screen_window p) {
    FakeScreenSlot *s = slotFromWindow(p);
    trace("destroy-window");
    if (!s) {
        return -1;
    }
    s->has_window = 0;
    s->has_buffer = 0;
    return 0;
}

int screen_set_window_property_iv(nbt_screen_window w, int prop, const int *value) {
    FakeScreenSlot *s = slotFromWindow(w);
    if (!s || !value) {
        return -1;
    }
    if (prop == NBT_SCREEN_PROPERTY_FORMAT) {
        s->format = *value;
    } else if (prop == NBT_SCREEN_PROPERTY_BUFFER_SIZE) {
        char event[64];
        if (value[0] <= 0 || value[1] <= 0 ||
            (size_t)value[0] * (size_t)value[1] * 4 > sizeof(s->pixels)) {
            return -1;
        }
        s->width = value[0];
        s->height = value[1];
        snprintf(event, sizeof(event), "size %dx%d", s->width, s->height);
        trace(event);
    } else if (prop == NBT_SCREEN_PROPERTY_VISIBLE) {
        if (*value && !s->posted) {
            return -1;
        }
        s->visible = *value;
        trace(s->visible ? "show" : "hide");
    }
    return 0;
}

int screen_set_window_property_cv(nbt_screen_window w, int prop, int length, const char *value) {
    FakeScreenSlot *s = slotFromWindow(w);
    char event[64];
    if (!s || prop != NBT_SCREEN_PROPERTY_ID_STRING || !value || length <= 0 || length > 32) {
        return -1;
    }
    memcpy(s->id, value, (size_t)length);
    s->id[length] = '\0';
    snprintf(event, sizeof(event), "id %.*s", length, value);
    trace(event);
    return 0;
}

int screen_create_window_buffers(nbt_screen_window w, int n) {
    FakeScreenSlot *s = slotFromWindow(w);
    if (!s || n != 1) {
        return -1;
    }
    s->has_buffer = 1;
    return 0;
}

int screen_get_window_property_pv(nbt_screen_window w, int prop, void **value) {
    FakeScreenSlot *s = slotFromWindow(w);
    if (!s || !s->has_buffer || prop != NBT_SCREEN_PROPERTY_RENDER_BUFFERS || !value) {
        return -1;
    }
    *value = &s->has_buffer;
    return 0;
}

int screen_get_buffer_property_pv(nbt_screen_buffer b, int prop, void **value) {
    FakeScreenSlot *s = slotFromBuffer(b);
    if (!s || prop != NBT_SCREEN_PROPERTY_POINTER || !value) {
        return -1;
    }
    *value = s->pixels;
    return 0;
}

int screen_get_buffer_property_iv(nbt_screen_buffer b, int prop, int *value) {
    FakeScreenSlot *s = slotFromBuffer(b);
    if (!s || !value) {
        return -1;
    }
    if (prop == NBT_SCREEN_PROPERTY_STRIDE) {
        *value = s->width * 4;
    } else if (prop == NBT_SCREEN_PROPERTY_SIZE) {
        *value = s->width * s->height * 4;
    } else {
        return -1;
    }
    return 0;
}

static int has_nonzero_pixel(const unsigned char *pixels, int width, int height) {
    const size_t total = (size_t)width * (size_t)height * 4u;
    for (size_t i = 0; i < total; i += 4) {
        if (pixels[i] || pixels[i + 1] || pixels[i + 2] || pixels[i + 3]) {
            return 1;
        }
    }
    return 0;
}

int screen_post_window(nbt_screen_window w, nbt_screen_buffer b, int n, const int *r, int flags) {
    FakeScreenSlot *s = slotFromWindow(w);
    if (!s || slotFromBuffer(b) != s || n != 1 || !r || flags != 0) {
        return -1;
    }
    s->posted = 1;
    trace("post");
    if (s->format == NBT_SCREEN_FORMAT_RGBA8888 && s->id[0]) {
        const char *vpath = getenv("NBT_FAKE_VOLUME_SCREEN_LOG");
        FILE *vfile;
        if (vpath && (vfile = fopen(vpath, "a")) != NULL) {
            fprintf(vfile, "%s %s\n", s->id,
                    has_nonzero_pixel(s->pixels, s->width, s->height) ? "drawn" : "clear");
            fclose(vfile);
        }
    }
    {
        const char *path = getenv("NBT_FAKE_SCREEN_TIME_LOG");
        struct timespec now;
        FILE *file;
        if (path && !clock_gettime(CLOCK_MONOTONIC, &now) && (file = fopen(path, "a")) != NULL) {
            fprintf(file, "%ld.%03ld\n", (long)now.tv_sec, now.tv_nsec / 1000000L);
            fclose(file);
        }
    }
    {
        const char *path = getenv("NBT_FAKE_SCREEN_COLOR_LOG");
        FILE *file;
        uint32_t color;
        memcpy(&color, s->pixels, sizeof(color));
        if (path && (file = fopen(path, "a")) != NULL) {
            fprintf(file, "%08x\n", (unsigned)color);
            fclose(file);
        }
    }
    return 0;
}
