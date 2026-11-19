#include "nbt/worker/wifi_direct_most.hpp"

#include <string.h>

namespace nbt {
namespace worker {
namespace {

uint16_t readLe16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

bool appendUtf8(uint32_t cp, char *out, size_t outCap, size_t *used) {
    uint8_t tmp[4];
    size_t n = 0;
    if (cp == 0 || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) {
        return false;
    }
    if (cp <= 0x7fu) {
        tmp[n++] = static_cast<uint8_t>(cp);
    } else if (cp <= 0x7ffu) {
        tmp[n++] = static_cast<uint8_t>(0xc0u | (cp >> 6));
        tmp[n++] = static_cast<uint8_t>(0x80u | (cp & 0x3fu));
    } else if (cp <= 0xffffu) {
        tmp[n++] = static_cast<uint8_t>(0xe0u | (cp >> 12));
        tmp[n++] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3fu));
        tmp[n++] = static_cast<uint8_t>(0x80u | (cp & 0x3fu));
    } else {
        tmp[n++] = static_cast<uint8_t>(0xf0u | (cp >> 18));
        tmp[n++] = static_cast<uint8_t>(0x80u | ((cp >> 12) & 0x3fu));
        tmp[n++] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3fu));
        tmp[n++] = static_cast<uint8_t>(0x80u | (cp & 0x3fu));
    }
    if (*used + n >= outCap) {
        return false;
    }
    memcpy(out + *used, tmp, n);
    *used += n;
    return true;
}

bool decodeMostUtf16(const uint8_t *data, size_t len, size_t *pos, char *out, size_t outCap) {
    size_t used = 0;
    while (*pos + 1 < len) {
        const uint16_t unit = readLe16(data + *pos);
        *pos += 2;
        if (unit == 0) {
            out[used] = 0;
            return true;
        }
        uint32_t cp = unit;
        if (unit >= 0xd800u && unit <= 0xdbffu) {
            if (*pos + 1 >= len) {
                return false;
            }
            const uint16_t low = readLe16(data + *pos);
            *pos += 2;
            if (low < 0xdc00u || low > 0xdfffu) {
                return false;
            }
            cp = 0x10000u + (static_cast<uint32_t>(unit - 0xd800u) << 10) +
                 static_cast<uint32_t>(low - 0xdc00u);
        }
        if (!appendUtf8(cp, out, outCap, &used)) {
            return false;
        }
    }
    return false;
}

bool decodeMostString(const uint8_t *data, size_t len, size_t *pos, char *out, size_t outCap) {
    if (!out || !outCap || *pos >= len) {
        return false;
    }
    const uint8_t encoding = data[(*pos)++];
    if (encoding == 0) {
        if (!decodeMostUtf16(data, len, pos, out, outCap)) {
            core::secureClear(out, outCap);
            return false;
        }
        return true;
    }
    if (encoding > 6) {
        return false;
    }
    size_t used = 0;
    while (*pos < len) {
        const uint8_t ch = data[(*pos)++];
        if (ch == 0) {
            out[used] = 0;
            return true;
        }
        if (used + 1 >= outCap) {
            core::secureClear(out, outCap);
            return false;
        }
        out[used++] = static_cast<char>(ch);
    }
    core::secureClear(out, outCap);
    return false;
}

bool isWfdControlMsg(const nbt_most_wire_message &msg, uint16_t fkt) {
    return msg.fblock == kMostFblockWlan && msg.inst == kMostInstWfd && msg.fkt == fkt &&
           (!msg.length || msg.data != NULL);
}

bool encodeSend(uint16_t fkt, uint8_t op, uint32_t request, const uint8_t *payload,
                size_t payloadLen, uint8_t *out, size_t capacity, size_t *length) {
    nbt_most_wire_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.request = request;
    msg.fblock = kMostFblockWlan;
    msg.inst = kMostInstWfd;
    msg.fkt = fkt;
    msg.op = op;
    msg.data = payload;
    msg.length = static_cast<uint32_t>(payloadLen);
    const long n = nbt_most_wire_encode_message(NBT_MOST_WIRE_SEND, &msg, out, capacity);
    if (n <= 0) {
        return false;
    }
    if (length) {
        *length = static_cast<size_t>(n);
    }
    return true;
}

} // namespace

bool validWfdSsid(const char *ssid) {
    if (!ssid || !ssid[0]) {
        return false;
    }
    const size_t len = strlen(ssid);
    if (len > kWifiSsidMax) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(ssid[i]);
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

bool validWfdPassphrase(const char *pass, size_t length) {
    if (!pass || length < 8 || length > kWifiPassMax) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(pass[i]);
        if (c < 0x20 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

bool encodeWfdGet(uint16_t fkt, uint32_t request, uint8_t *out, size_t capacity, size_t *length) {
    return encodeSend(fkt, kMostOpGet, request, NULL, 0, out, capacity, length);
}

bool encodeWfdSelectState(uint16_t handle, uint8_t state, uint32_t request, uint8_t *out,
                          size_t capacity, size_t *length) {
    const uint8_t payload[3] = {static_cast<uint8_t>(handle & 0xffu),
                                static_cast<uint8_t>((handle >> 8) & 0xffu), state};
    return encodeSend(kFktSelectWlanState3, kMostOpStartResultAck, request, payload,
                      sizeof(payload), out, capacity, length);
}

bool decodeWfdState(const nbt_most_wire_message &msg, WfdStateReply *out) {
    if (!out || !isWfdControlMsg(msg, kFktWlanState3) || msg.length < 1) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->op = msg.op;
    if (msg.op == kMostOpStatus) {
        out->state = msg.data[0];
        return true;
    }
    if (msg.op == kMostOpError) {
        out->errorCode = msg.data[0];
        return true;
    }
    return false;
}

bool decodeWfdSelect(const nbt_most_wire_message &msg, WfdSelectReply *out) {
    if (!out || !isWfdControlMsg(msg, kFktSelectWlanState3) || msg.length < 2) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->op = msg.op;
    out->handle = readLe16(msg.data);
    if (msg.op == kMostOpProcessingAck) {
        return true;
    }
    if (msg.op == kMostOpResultAck) {
        out->hasState = msg.length >= 3;
        out->state = out->hasState ? msg.data[2] : 0;
        return true;
    }
    if (msg.op == kMostOpErrorAck) {
        out->errorCode = msg.length >= 3 ? msg.data[2] : 0;
        return true;
    }
    return false;
}

bool decodeWfdConfig(const nbt_most_wire_message &msg, WfdConfig *out) {
    if (!out || !isWfdControlMsg(msg, kFktWlanConfiguration) || msg.op != kMostOpStatus) {
        return false;
    }
    WfdConfig parsed;
    char scratch[65];
    core::SecureString<kWifiPassMax> key;
    size_t pos = 0;
    // Wire fields 0..9 match HmiMain3D/Connectivity WLANConfiguration Property order.
    bool ok =
        decodeMostString(msg.data, msg.length, &pos, scratch, sizeof(scratch)) && // MACAddress
        decodeMostString(msg.data, msg.length, &pos, scratch, sizeof(scratch)) && // DeviceName
        decodeMostString(msg.data, msg.length, &pos, parsed.ipAddress, sizeof(parsed.ipAddress)) &&
        decodeMostString(msg.data, msg.length, &pos, scratch, 32) && // PrimaryDeviceType
        decodeMostString(msg.data, msg.length, &pos, scratch, 32) && // SecondaryDeviceType
        pos < msg.length;
    if (ok) {
        ++pos; // WiFiDisplayEnable
    }
    ok = ok && decodeMostString(msg.data, msg.length, &pos, parsed.ssid, sizeof(parsed.ssid));
    if (ok && pos < msg.length) {
        parsed.hasSsidVisible = true;
        parsed.ssidVisible = msg.data[pos++];
    }
    if (ok && pos < msg.length) {
        ++pos; // WLANAccessPointSecurity3
    }
    if (ok && pos < msg.length) {
        if (!decodeMostString(msg.data, msg.length, &pos, key.data(), kWifiPassMax + 1)) {
            ok = false;
        } else {
            const size_t len = strlen(key.data());
            if (len && validWfdPassphrase(key.data(), len)) {
                ok = parsed.passphrase.set(key.data(), len);
            }
        }
    }
    key.wipe();
    core::secureClear(scratch, sizeof(scratch));
    if (!ok || !out->copyFrom(parsed)) {
        parsed.clear();
        return false;
    }
    parsed.clear();
    return true;
}

} // namespace worker
} // namespace nbt
