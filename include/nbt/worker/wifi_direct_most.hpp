// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_WIFI_DIRECT_MOST_HPP
#define NBT_WORKER_WIFI_DIRECT_MOST_HPP

#include "nbt/core/secure.hpp"
#include "nbt/most_wire.h"
#include "nbt/worker/wifi_direct.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace worker {

static const uint8_t kMostFblockWlan = 0xa0;
static const uint8_t kMostInstWfd = 0x03;
static const uint16_t kMostKeyWfd = 0xa003;

static const uint16_t kFktWlanState3 = 0xc10;
static const uint16_t kFktSelectWlanState3 = 0xc11;
static const uint16_t kFktWlanConfiguration = 0xc12;

/* MOST OpTypes. A method's reports: ErrorAck 9, ProcessingAck A, Processing B,
 * Result C, ResultAck D, Error F; a property's: Status C, Error F. dev66 had
 * ResultAck and ProcessingAck swapped: the car's "group on" went unread, the
 * worker timed out and switched the group off again. */
static const uint8_t kMostOpGet = 0x01;
static const uint8_t kMostOpStartResultAck = 0x06;
static const uint8_t kMostOpErrorAck = 0x09;
static const uint8_t kMostOpProcessingAck = 0x0a;
static const uint8_t kMostOpStatus = 0x0c;
static const uint8_t kMostOpResultAck = 0x0d;
static const uint8_t kMostOpError = 0x0f;

struct WfdStateReply {
    uint8_t op;
    uint8_t state;
    uint8_t errorCode;
};

struct WfdSelectReply {
    uint8_t op;
    uint16_t handle;
    uint8_t state;
    bool hasState;
    uint8_t errorCode;
};

struct WfdConfig {
    WfdConfig() : hasSsidVisible(false), ssidVisible(0), passphrase() {
        ipAddress[0] = 0;
        ssid[0] = 0;
    }
    WfdConfig(const WfdConfig &) = delete;
    WfdConfig &operator=(const WfdConfig &) = delete;
    ~WfdConfig() {
        clear();
    }
    void clear() {
        memset(ipAddress, 0, sizeof(ipAddress));
        memset(ssid, 0, sizeof(ssid));
        hasSsidVisible = false;
        ssidVisible = 0;
        passphrase.wipe();
    }
    bool copyFrom(const WfdConfig &other) {
        clear();
        memcpy(ipAddress, other.ipAddress, sizeof(ipAddress));
        memcpy(ssid, other.ssid, sizeof(ssid));
        hasSsidVisible = other.hasSsidVisible;
        ssidVisible = other.ssidVisible;
        return !other.passphrase.size() ||
               passphrase.set(other.passphrase.data(), other.passphrase.size());
    }

    char ipAddress[40];
    char ssid[kWifiSsidMax + 1];
    bool hasSsidVisible;
    uint8_t ssidVisible;
    core::SecureString<kWifiPassMax> passphrase;
};

bool validWfdSsid(const char *ssid);
bool validWfdPassphrase(const char *pass, size_t length);

bool encodeWfdGet(uint16_t fkt, uint32_t request, uint8_t *out, size_t capacity, size_t *length);
bool encodeWfdSelectState(uint16_t handle, uint8_t state, uint32_t request, uint8_t *out,
                          size_t capacity, size_t *length);

bool decodeWfdState(const nbt_most_wire_message &msg, WfdStateReply *out);
bool decodeWfdSelect(const nbt_most_wire_message &msg, WfdSelectReply *out);
bool decodeWfdConfig(const nbt_most_wire_message &msg, WfdConfig *out);

} // namespace worker
} // namespace nbt

#endif
