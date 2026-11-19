// SPDX-License-Identifier: MPL-2.0
#include "nbt/most/codec.hpp"

#include <string.h>

namespace {

const size_t kHeader = 32;

uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint16_t le16(const unsigned char *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

int put32(unsigned char *out, size_t size, size_t *used, uint32_t value) {
    while (*used % 4) {
        if (*used >= size) {
            return -1;
        }
        out[(*used)++] = 0;
    }
    if (size - *used < 4) {
        return -1;
    }
    out[(*used)++] = (unsigned char)value;
    out[(*used)++] = (unsigned char)(value >> 8);
    out[(*used)++] = (unsigned char)(value >> 16);
    out[(*used)++] = (unsigned char)(value >> 24);
    return 0;
}

long controls(uint32_t client, const uint16_t *keys, size_t count, unsigned char *out,
              size_t size) {
    size_t used = 0;
    if (!keys || !out || !count || count > 32 || put32(out, size, &used, client) ||
        put32(out, size, &used, (uint32_t)count)) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (size - used < 2) {
            return -1;
        }
        out[used++] = (unsigned char)keys[i];
        out[used++] = (unsigned char)(keys[i] >> 8);
    }
    while (used % 4) {
        if (used >= size) {
            return -1;
        }
        out[used++] = 0;
    }
    return (long)used;
}

} // namespace

const uint32_t nbt_most_subscriptions[] = {0xc0000002u, 0xc0000003u, 0xc0000001u, 0x80000001u,
                                           0x80000002u, 0x80000003u, 0x80000004u, 0x80000005u,
                                           0x80000006u, 0x80000007u};
const size_t nbt_most_subscription_count =
    sizeof(nbt_most_subscriptions) / sizeof(nbt_most_subscriptions[0]);

namespace nbt {
namespace most {

int Codec::parseAny(const unsigned char *p, size_t length, nbt_most_message *m) {
    if (!p || !m || length < kHeader) {
        return -1;
    }
    memset(m, 0, sizeof(*m));
    m->tx_type = le32(p);
    m->variant_tag = le32(p + 4);
    m->variant_value = le32(p + 8);
    m->dst = le16(p + 12);
    m->src = le16(p + 14);
    m->fblock = p[16];
    m->inst = p[17];
    m->fkt = le16(p + 18);
    m->op = p[20];
    m->tag = le32(p + 24);
    const uint32_t n = le32(p + 28);
    if (n > length - kHeader) {
        return -1;
    }
    m->data = p + kHeader;
    m->length = n;
    if (m->variant_tag != 2) {
        return NBT_MOST_NO_INFO;
    }
    if (m->tx_type == 1 && m->variant_value >= 19 && m->variant_value <= 21) {
        return (int)NBT_MOST_SEG_FIRST + (int)(m->variant_value - 19);
    }
    if (m->variant_value >= 4 && m->variant_value <= 9) {
        return NBT_MOST_TX_STATUS;
    }
    return NBT_MOST_NORMAL;
}

int Codec::parse(const unsigned char *p, size_t length, nbt_most_message *m) {
    const int kind = parseAny(p, length, m);
    return kind == NBT_MOST_NORMAL || kind == NBT_MOST_NO_INFO ? 0 : -1;
}

int Codec::clientReply(const unsigned char *p, size_t length, uint32_t *client, uint16_t *device) {
    if (!p || !client || !device || length < 6) {
        return -1;
    }
    *client = le32(p);
    *device = le16(p + 4);
    return *client == NBT_MOST_CLIENT_REFUSED ? -1 : 0;
}

long Codec::processMessage(uint32_t client, uint16_t src, uint16_t dst, uint8_t fblock,
                           uint8_t inst, uint16_t fkt, uint8_t op, uint32_t tag,
                           const unsigned char *data, size_t length, unsigned char *out,
                           size_t size) {
    size_t used = 0;
    if (!out || (length && !data) || length > NBT_MOST_JOIN_MAX || fkt > 0xfff || op > 15 ||
        size < 4 + kHeader + length) {
        return -1;
    }
    memset(out, 0, 4 + kHeader);
    if (put32(out, size, &used, client) || put32(out, size, &used, 0) ||
        put32(out, size, &used, 1) || put32(out, size, &used, 0)) {
        return -1;
    }
    out[used++] = (unsigned char)dst;
    out[used++] = (unsigned char)(dst >> 8);
    out[used++] = (unsigned char)src;
    out[used++] = (unsigned char)(src >> 8);
    out[used++] = fblock;
    out[used++] = inst;
    out[used++] = (unsigned char)fkt;
    out[used++] = (unsigned char)(fkt >> 8);
    out[used++] = op;
    if (put32(out, size, &used, tag) || put32(out, size, &used, (uint32_t)length)) {
        return -1;
    }
    if (length) {
        memcpy(out + used, data, length);
    }
    return (long)(used + length);
}

long Codec::registry(const unsigned char *p, size_t length, nbt_most_registry_entry *entries,
                     size_t max) {
    if (!p || (max && !entries) || length < 4) {
        return -1;
    }
    const uint32_t count = le32(p);
    if (count > 0x7fffffffu || count > (length - 4) / 4) {
        return -1;
    }
    for (size_t i = 0; i < count && i < max; ++i) {
        entries[i].first = le16(p + 4 + 4 * i);
        entries[i].key = le16(p + 6 + 4 * i);
    }
    return (long)count;
}

int Codec::clientId(const unsigned char *p, size_t length, uint32_t *client) {
    if (!p || !client || length < 6) {
        return -1;
    }
    *client = le32(p);
    return *client == NBT_MOST_CLIENT_REFUSED ? -1 : 0;
}

long Codec::registerClient(const char *name, unsigned char *out, size_t size) {
    size_t used = 0;
    if (!name || !out) {
        return -1;
    }
    const size_t n = strlen(name);
    if (n == 0 || n > 64 || put32(out, size, &used, (uint32_t)n + 1) || size - used < n + 1) {
        return -1;
    }
    memcpy(out + used, name, n + 1);
    used += n + 1;
    while (used % 4) {
        if (used >= size) {
            return -1;
        }
        out[used++] = 0;
    }
    return (long)used;
}

long Codec::registerControls(uint32_t client, const uint16_t *keys, size_t count,
                             unsigned char *out, size_t size) {
    return controls(client, keys, count, out, size);
}

long Codec::unregisterControls(uint32_t client, const uint16_t *keys, size_t count,
                               unsigned char *out, size_t size) {
    return controls(client, keys, count, out, size);
}

} // namespace most
} // namespace nbt

extern "C" {

int nbt_most_parse_any(const unsigned char *p, size_t length, nbt_most_message *m) {
    return nbt::most::Codec::parseAny(p, length, m);
}

int nbt_most_parse(const unsigned char *p, size_t length, nbt_most_message *m) {
    return nbt::most::Codec::parse(p, length, m);
}

int nbt_most_client_reply(const unsigned char *p, size_t length, uint32_t *client,
                          uint16_t *device) {
    return nbt::most::Codec::clientReply(p, length, client, device);
}

long nbt_most_process_message(uint32_t client, uint16_t src, uint16_t dst, uint8_t fblock,
                              uint8_t inst, uint16_t fkt, uint8_t op, uint32_t tag,
                              const unsigned char *data, size_t length, unsigned char *out,
                              size_t size) {
    return nbt::most::Codec::processMessage(client, src, dst, fblock, inst, fkt, op, tag, data,
                                            length, out, size);
}

long nbt_most_registry(const unsigned char *p, size_t length, nbt_most_registry_entry *entries,
                       size_t max) {
    return nbt::most::Codec::registry(p, length, entries, max);
}

int nbt_most_client_id(const unsigned char *p, size_t length, uint32_t *client) {
    return nbt::most::Codec::clientId(p, length, client);
}

long nbt_most_register_client(const char *name, unsigned char *out, size_t size) {
    return nbt::most::Codec::registerClient(name, out, size);
}

long nbt_most_register_controls(uint32_t client, const uint16_t *keys, size_t count,
                                unsigned char *out, size_t size) {
    return nbt::most::Codec::registerControls(client, keys, count, out, size);
}

long nbt_most_unregister_controls(uint32_t client, const uint16_t *keys, size_t count,
                                  unsigned char *out, size_t size) {
    return nbt::most::Codec::unregisterControls(client, keys, count, out, size);
}

} // extern "C"
