// SPDX-License-Identifier: MPL-2.0
// The status wire line (capability `status`, docs/app-bridge.md).
#include "nbt/bridge.h"

#include <stdio.h>
#include <string.h>

int nbt_bridge_status_parse(const char *line, size_t length, uint64_t *sequence,
                            char text[NBT_BRIDGE_STATUS_MAX + 1]) {
    uint64_t value = 0;
    size_t i = 0;
    if (!line || !sequence || !text || length < 4 || length > NBT_BRIDGE_STATUS_LINE_MAX) {
        return -1;
    }
    while (i < length && line[i] >= '0' && line[i] <= '9') {
        const unsigned digit = static_cast<unsigned>(line[i] - '0');
        if (i >= 20 || value > (UINT64_MAX - digit) / 10u) {
            return -1;
        }
        value = value * 10u + digit;
        ++i;
    }
    if (!i || (i > 1 && line[0] == '0') || i >= length || line[i++] != ' ') {
        return -1;
    }
    const size_t start = i;
    if (start >= length) {
        return -1;
    }
    const size_t n = length - start - 1;
    if (!n || n > NBT_BRIDGE_STATUS_MAX || line[length - 1] != '\n') {
        return -1;
    }
    for (; i < length - 1; ++i) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if (c < 32 || c > 126) {
            return -1;
        }
    }
    memcpy(text, line + start, n);
    text[n] = 0;
    *sequence = value;
    return 0;
}

int nbt_bridge_status_format(uint64_t sequence, const char *text, char *out, size_t size) {
    if (!text || !out) {
        return -1;
    }
    size_t length = 0;
    while (length <= NBT_BRIDGE_STATUS_MAX && text[length]) {
        ++length;
    }
    if (!length || length > NBT_BRIDGE_STATUS_MAX) {
        return -1;
    }
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 32 || c > 126) {
            return -1;
        }
    }
    const int n = snprintf(out, size, "%llu %s\n", static_cast<unsigned long long>(sequence), text);
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : n;
}
