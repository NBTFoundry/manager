#include "nbt/display/layer_manager.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace display {
namespace {

const char kLayer[] = "0x80000000";

int run(const char *a, const char *b, const char *c, const char *d = NULL, const char *e = NULL,
        const char *f = NULL, const char *g = NULL, const char *h = NULL, const char *i = NULL) {
    const char *const args[] = {a, b, c, d, e, f, g, h, i, NULL};
    char output[256];
    return control(args, output, sizeof(output));
}

// The surfaces after "surface render order:" in `get layer` output, e.g.
// "    - surface render order: 1281(0x501), 1280(0x500),". Returns how many
// (at most `max` are kept), or -1 when the line is missing or malformed.
int layerOrder(const char *dump, long *ids, int max) {
    const char *at = strstr(dump, "surface render order:");
    if (!at) {
        return -1;
    }
    at += strlen("surface render order:");
    int count = 0;
    for (;;) {
        while (*at == ' ' || *at == ',' || *at == '\t') {
            ++at;
        }
        if (!*at || *at == '\n' || *at == '\r') {
            return count;
        }
        if (*at < '0' || *at > '9') {
            return -1;
        }
        char *end = NULL;
        const long id = strtol(at, &end, 10);
        if (count < max) {
            ids[count] = id;
        }
        ++count;
        at = end;
        while (*at && *at != ',' && *at != '\n') {
            ++at;
        }
    }
}

// The render order of layer 0x80000000, or -1 when LayerManager gave none.
int hmiLayer(long *ids, int max) {
    char output[1024];
    const char *const args[] = {"get", "layer", kLayer, NULL};
    return control(args, output, sizeof(output)) < 0 ? -1 : layerOrder(output, ids, max);
}

bool ours(long id) {
    return id == kManagerSurface || id == kAppSurface || id == kVolumeSurface;
}

bool panelAcceptsSwap(const long *ids, int count) {
    if (count == 1 && ids[0] == 256) {
        return true;
    }
    if (count < 1) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (!ours(ids[i])) {
            return false;
        }
    }
    return true;
}

void setSurfaceRegions(const char *id, int panelWidth, int panelHeight) {
    char w[16], h[16];
    snprintf(w, sizeof(w), "%d", panelWidth);
    snprintf(h, sizeof(h), "%d", panelHeight);
    run("set", "surface", id, "source", "region", "0", "0", w, h);
    run("set", "surface", id, "destination", "region", "0", "0", w, h);
}

} // namespace

bool surfaceBound(int surface) {
    char id[16], output[1024];
    snprintf(id, sizeof(id), "%d", surface);
    const char *const args[] = {"get", "surface", id, NULL};
    if (control(args, output, sizeof(output)) < 0) {
        return false;
    }
    // 4294967295 (or nothing) means no window is bound yet.
    const char *native = strstr(output, "native surface:");
    const long long value = native ? strtoll(native + 15, NULL, 10) : -1;
    return value > 0 && value != 4294967295LL;
}

bool panelSize(int &width, int &height) {
    char output[1024];
    const char *const args[] = {"get", "screen", "0", NULL};
    if (control(args, output, sizeof(output)) < 0) {
        return false;
    }
    // "- resolution:           x=1440, y=540"
    const char *line = strstr(output, "resolution:");
    const char *x = line ? strstr(line, "x=") : NULL;
    const char *y = line ? strstr(line, "y=") : NULL;
    width = x ? atoi(x + 2) : 0;
    height = y ? atoi(y + 2) : 0;
    return width > 0 && width <= 4096 && height > 0 && height <= 4096;
}

int holds(int surface) {
    long ids[8];
    const int count = hmiLayer(ids, 8);
    if (count < 0) {
        return -1;
    }
    for (int i = 0; i < count && i < 8; ++i) {
        if (ids[i] == surface) {
            return 1;
        }
    }
    return 0;
}

core::Status show(int surface, int *replaced, int overlay) {
    // Only iDrive, or our own surfaces, are ever replaced: with Android Auto
    // (or anything else) on the layer, its owner has the panel.
    long ids[8];
    const int count = hmiLayer(ids, 8);
    if (count < 0 || count > 8 || !panelAcceptsSwap(ids, count)) {
        return core::Status::error(EBUSY, count < 0 ? "LayerManager gave no HMI layer order"
                                                    : "another surface has the HMI layer");
    }
    int panelWidth = 0, panelHeight = 0;
    if (!panelSize(panelWidth, panelHeight)) {
        return core::Status::error(ENODEV, "LayerManager screen 0 size unknown");
    }
    char id[16], order[32];
    snprintf(id, sizeof(id), "%d", surface);
    snprintf(order, sizeof(order), "0x%x", static_cast<unsigned>(surface));
    // NBT-AAD's order for its own surface: the picture goes up first.
    run("set", "layer", kLayer, "render", "order", order);
    setSurfaceRegions(id, panelWidth, panelHeight);
    run("set", "surface", "256", "visibility", "0");
    for (int i = 0; i < count; ++i) {
        if (ids[i] != 256 && ids[i] != surface && ids[i] != overlay) {
            char other[16];
            snprintf(other, sizeof(other), "%ld", ids[i]);
            run("set", "surface", other, "visibility", "0");
        }
    }
    run("set", "surface", id, "visibility", "1");
    if (overlay > 0) {
        char ovId[16], ovOrder[32];
        snprintf(ovId, sizeof(ovId), "%d", overlay);
        snprintf(ovOrder, sizeof(ovOrder), "0x%x,0x%x", static_cast<unsigned>(overlay),
                 static_cast<unsigned>(surface));
        setSurfaceRegions(ovId, panelWidth, panelHeight);
        run("set", "surface", ovId, "visibility", "1");
        run("set", "layer", kLayer, "render", "order", ovOrder);
        if (holds(overlay) != 1 || holds(surface) != 1) {
            run("set", "layer", kLayer, "render", "order", order);
        }
    }
    if (holds(surface) != 1) {
        return core::Status::error(EIO, "LayerManager did not take the render order");
    }
    if (replaced) {
        long prev = ids[0];
        if (prev == kVolumeSurface && count > 1) {
            prev = ids[1];
        }
        *replaced = static_cast<int>(prev);
    }
    return core::Status();
}

bool attachOverlay(int overlay, int mainSurface) {
    if (overlay <= 0 || mainSurface <= 0) {
        return false;
    }
    int panelWidth = 0, panelHeight = 0;
    if (!panelSize(panelWidth, panelHeight)) {
        return false;
    }
    char ovId[16], ovOrder[32], plainOrder[16];
    snprintf(ovId, sizeof(ovId), "%d", overlay);
    snprintf(ovOrder, sizeof(ovOrder), "0x%x,0x%x", static_cast<unsigned>(overlay),
             static_cast<unsigned>(mainSurface));
    snprintf(plainOrder, sizeof(plainOrder), "0x%x", static_cast<unsigned>(mainSurface));
    setSurfaceRegions(ovId, panelWidth, panelHeight);
    run("set", "surface", ovId, "visibility", "1");
    run("set", "layer", kLayer, "render", "order", ovOrder);
    if (holds(overlay) == 1 && holds(mainSurface) == 1) {
        return true;
    }
    run("set", "layer", kLayer, "render", "order", plainOrder);
    return false;
}

bool restore() {
    long ids[8];
    const int count = hmiLayer(ids, 8);
    if (count < 0 || count > 8) {
        return false;
    }
    bool mine = count == 0;
    for (int i = 0; i < count; ++i) {
        if (!ours(ids[i]) && ids[i] != 256) {
            return false; // Android Auto, or anything else, keeps the panel
        }
        mine = mine || ours(ids[i]);
    }
    if (!mine) {
        return false;
    }
    run("set", "layer", kLayer, "render", "order", "0x100");
    run("set", "surface", "256", "visibility", "1");
    for (int i = 0; i < count; ++i) {
        if (ours(ids[i])) {
            char id[16];
            snprintf(id, sizeof(id), "%ld", ids[i]);
            run("set", "surface", id, "visibility", "0");
        }
    }
    return true;
}

} // namespace display
} // namespace nbt
