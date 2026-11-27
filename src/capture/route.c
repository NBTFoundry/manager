// SPDX-License-Identifier: MPL-2.0
/* The hidden audio connection: its note and the connection-table filter. */
#include "nbt/capture/route.h"

#include "nbt/capture/control.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ROUTE_MAGIC "NBTROUTE1 "
#define ROUTE_MAX 40u
#define DSI_HEADER_BYTES 16u
#define MOST_HEADER_BYTES 32u
#define DSI_INFO_PROCESS_MSG 0x80000001u
#define MOST_OP_STATUS 0x0Cu
#define ROW_BYTES 3u

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

int nbt_capture_route_parse(const char *text, size_t size, int32_t *pid, uint16_t *conn) {
    const size_t magic = sizeof(ROUTE_MAGIC) - 1u;
    uint32_t p = 0, c = 0;
    size_t i = magic, digits = 0;
    if (!text || !pid || !conn || size < magic + 4u || size > ROUTE_MAX ||
        memcmp(text, ROUTE_MAGIC, magic) != 0) {
        return -1;
    }
    for (; i < size && text[i] >= '0' && text[i] <= '9' && digits < 10u; ++i, ++digits) {
        p = p * 10u + (uint32_t)(text[i] - '0');
        if (p > 0x7fffffffu) {
            return -1;
        }
    }
    /* One spelling per value: no leading zero, lower-case hex. */
    if (!digits || !p || text[magic] == '0' || i >= size || text[i++] != ' ' || i >= size ||
        text[i] == '0') {
        return -1;
    }
    for (digits = 0; i < size && digits < 4u; ++i, ++digits) {
        const char ch = text[i];
        if (ch >= '0' && ch <= '9') {
            c = c * 16u + (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            c = c * 16u + (uint32_t)(ch - 'a') + 10u;
        } else {
            break;
        }
    }
    if (!digits || !c || i + 1u != size || text[i] != '\n') {
        return -1;
    }
    *pid = (int32_t)p;
    *conn = (uint16_t)c;
    return 0;
}

int nbt_capture_route_format(int32_t pid, uint16_t conn, char *out, size_t size) {
    int n;
    if (!out || pid <= 0 || !conn) {
        return -1;
    }
    n = snprintf(out, size, ROUTE_MAGIC "%ld %x\n", (long)pid, (unsigned)conn);
    return n > 0 && (size_t)n < size ? n : -1;
}

int nbt_capture_route_read(const char *path, int32_t *pid, uint16_t *conn) {
    char text[ROUTE_MAX + 1u];
    size_t used = 0;
    const int fd = nbt_capture_open_shmem_checked(path, 0);
    if (fd < 0) {
        return -1;
    }
    for (;;) {
        const ssize_t n = read(fd, text + used, sizeof(text) - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
        if (used == sizeof(text)) {
            break;
        }
    }
    close(fd);
    return used <= ROUTE_MAX ? nbt_capture_route_parse(text, used, pid, conn) : -1;
}

nbt_capture_table_result nbt_capture_table_hide(unsigned char *bytes, size_t length, uint16_t conn,
                                                int rewrite) {
    unsigned char *p, *data;
    uint32_t tx_type, variant_value, data_len, rows, kept = 0, r;
    size_t plen;
    if (!bytes || !conn || length < DSI_HEADER_BYTES + MOST_HEADER_BYTES ||
        le32(bytes + 8u) != DSI_INFO_PROCESS_MSG) {
        return NBT_CAPTURE_TABLE_NONE;
    }
    p = bytes + DSI_HEADER_BYTES;
    plen = length - DSI_HEADER_BYTES;
    tx_type = le32(p);
    variant_value = le32(p + 8u);
    data_len = le32(p + 28u);
    /* The same messages nbt_capture_classify_bytes reads: whole, not TX status. */
    if (le32(p + 4u) != 2u || data_len > plen - MOST_HEADER_BYTES ||
        (tx_type == 1u && variant_value >= 19u && variant_value <= 21u) ||
        (variant_value >= 4u && variant_value <= 9u)) {
        return NBT_CAPTURE_TABLE_NONE;
    }
    if (p[16] != NBT_CAPTURE_FBLOCK_CONNECTION_MASTER ||
        (uint16_t)((uint16_t)p[18] | ((uint16_t)p[19] << 8)) != NBT_CAPTURE_FKT_CONNECTION_TABLE ||
        p[20] != MOST_OP_STATUS || data_len < 2u + ROW_BYTES || (data_len - 2u) % ROW_BYTES != 0u) {
        return NBT_CAPTURE_TABLE_NONE;
    }
    data = p + MOST_HEADER_BYTES;
    rows = (data_len - 2u) / ROW_BYTES;
    if (rows > NBT_CAPTURE_TABLE_ROWS_MAX) {
        return NBT_CAPTURE_TABLE_NONE;
    }
    if (data[0] != 0u) { /* one row, at position data[0]: all of it or nothing */
        return rows == 1u && data[1] == 0u && data[2] == (unsigned char)(conn >> 8) &&
                       data[3] == (unsigned char)conn
                   ? NBT_CAPTURE_TABLE_ROW
                   : NBT_CAPTURE_TABLE_NONE;
    }
    if (data[1] != 0u) {
        return NBT_CAPTURE_TABLE_NONE;
    }
    for (r = 0; r < rows; ++r) {
        const unsigned char *row = data + 2u + r * ROW_BYTES;
        if (row[0] != (unsigned char)(conn >> 8) || row[1] != (unsigned char)conn) {
            ++kept;
        }
    }
    if (kept == rows) {
        return NBT_CAPTURE_TABLE_NONE;
    }
    if (rewrite) {
        uint32_t to = 0;
        for (r = 0; r < rows; ++r) {
            const unsigned char *row = data + 2u + r * ROW_BYTES;
            if (row[0] != (unsigned char)(conn >> 8) || row[1] != (unsigned char)conn) {
                memmove(data + 2u + to * ROW_BYTES, row, ROW_BYTES);
                ++to;
            }
        }
        memset(data + 2u + kept * ROW_BYTES, 0, (size_t)(rows - kept) * ROW_BYTES);
        put_le32(p + 28u, 2u + kept * ROW_BYTES);
    }
    return NBT_CAPTURE_TABLE_REWRITTEN;
}
