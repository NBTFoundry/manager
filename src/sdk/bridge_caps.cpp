#include "nbt/bridge.h"
#include <string.h>
void nbt_bridge_caps_without(const char *caps, const char *name,
                             char out[NBT_BRIDGE_CAPS_MAX + 1]) {
    if (!out) {
        return;
    }
    if (!caps || !name || !nbt_bridge_caps_valid(caps)) {
        out[0] = 0;
        return;
    }
    size_t used = 0;
    const size_t n = strlen(name);
    const char *p = caps;
    out[0] = 0;
    while (*p) {
        const char *end = strchr(p, ',');
        const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
        if (len != n || memcmp(p, name, n)) {
            if (used) {
                out[used++] = ',';
            }
            memcpy(out + used, p, len);
            used += len;
            out[used] = 0;
        }
        p += len;
        if (*p) {
            ++p;
        }
    }
}
