// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/wifi_direct_controller.hpp"

#include "nbt/core/system.hpp"
#include "nbt/log.h"
#include "nbt/worker/android_auto_wifi.hpp"
#include "nbt/worker/phone_network.hpp"
#include "nbt/worker/wfd_health.hpp"

#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

const uint64_t kPrepareTimeoutMs = 10000;
const uint64_t kRestoreTimeoutMs = 2000;

uint64_t configuredTimeout(const char *envName, uint64_t fallbackMs) {
#ifdef NBT_TESTING
    const char *env = getenv(envName);
    if (env && env[0]) {
        char *end = NULL;
        const unsigned long val = strtoul(env, &end, 10);
        if (end != env && *end == 0 && val >= 50 && val <= 30000) {
            return static_cast<uint64_t>(val);
        }
    }
#else
    (void)envName;
#endif
    return fallbackMs;
}

uint64_t currentMonotonic(uint64_t fallbackMs) {
    const core::Result<uint64_t> now = core::monotonicMs();
    return now.get() ? now.value() : fallbackMs;
}

/* WFDPass.conf first: dev69 in the car, MOST's WLANAccessPointKey differed
 * from it and phones given MOST's dropped after associating (no DHCP lease);
 * dev45's working pairing used WFDPass.conf. MOST's only when it is unreadable. */
bool resolvePassphrase(const WfdConfig &cfg, ReadyWifiCredentials *out) {
    char raw[kWifiPassMax + 1] = {0};
    const bool ok = readSavedWfdPassphrase(raw) && validWfdPassphrase(raw, strlen(raw)) &&
                    out->pass.set(raw, strlen(raw));
    core::secureClear(raw, sizeof(raw));
    if (ok) {
        out->source = "WFDPass.conf";
        return true;
    }
    if (cfg.passphrase.size() >= 8) {
        out->source = "MOST WLANConfiguration";
        return out->pass.set(cfg.passphrase.data(), cfg.passphrase.size());
    }
    return false;
}

} // namespace

WifiDirectController::WifiDirectController()
    : client_(), unavailableLogged_(false), enabledByUs_(false) {
}

WifiDirectController::~WifiDirectController() {
    shutdown();
}

void WifiDirectController::initialize() {
    client_.initialize();
}

int WifiDirectController::fd() const {
    return client_.fd();
}

void WifiDirectController::sets(fd_set *readers, int *highest) const {
    client_.sets(readers, highest);
}

void WifiDirectController::poll(uint64_t nowMs) {
    client_.poll(nowMs);
}

bool WifiDirectController::requiredForInterface(const char *iface) const {
    if (!iface) {
        return false;
    }
    if (!strcmp(iface, "wfd0")) {
        return true;
    }
#ifdef NBT_TESTING
    const char *mock = getenv("NBT_TEST_WFD_MOCK_MOST");
    if (client_.active() && mock && mock[0]) {
        return true;
    }
#endif
    return false;
}

bool WifiDirectController::prepareSession(const char *iface, uint64_t nowMs,
                                          ReadyWifiCredentials *out) {
    if (!out) {
        return false;
    }
    out->clear();
    if (!requiredForInterface(iface)) {
        return true;
    }
    out->managed = true;
    if (!client_.active()) {
        if (!unavailableLogged_) {
            unavailableLogged_ = true;
            nbt_logf("Wi-Fi Direct: MOST unavailable; cannot start group on %s", iface);
        }
        return false;
    }
    const uint64_t deadlineMs =
        nowMs + configuredTimeout("NBT_TEST_WFD_TIMEOUT_MS", kPrepareTimeoutMs);
    client_.poll(nowMs);
    if (!client_.waitReady(deadlineMs)) {
        return false;
    }
    uint8_t state = 0;
    if (!client_.queryState(deadlineMs, &state)) {
        return false;
    }
    if (state == 0) {
        nbt_logf("Wi-Fi Direct: enabling A0.03 group");
        if (!client_.selectState(1, deadlineMs)) {
            if (client_.cachedState() == 1) {
                enabledByUs_ = true;
                restore(currentMonotonic(nowMs));
            }
            return false;
        }
        enabledByUs_ = true;
        if (client_.cachedState() != 1 && (!client_.queryState(deadlineMs, &state) || state != 1)) {
            nbt_logf("Wi-Fi Direct: WLANState3 remained %u after enable",
                     static_cast<unsigned>(client_.cachedState()));
            restore(currentMonotonic(nowMs));
            return false;
        }
    } else if (state != 1) {
        nbt_logf("Wi-Fi Direct: unexpected WLANState3=%u", static_cast<unsigned>(state));
        return false;
    }
    WfdConfig cfg;
    if (!client_.queryConfig(deadlineMs, &cfg) || !validWfdSsid(cfg.ssid) ||
        (cfg.hasSsidVisible && cfg.ssidVisible == 0) || !resolvePassphrase(cfg, out)) {
        nbt_logf("Wi-Fi Direct: live configuration unusable (visible=%u)",
                 cfg.hasSsidVisible ? static_cast<unsigned>(cfg.ssidVisible) : 1u);
        out->clear();
        restore(currentMonotonic(nowMs));
        return false;
    }
    char ip[40] = {0};
    bool ready = interfaceReady(iface, ip);
    while (!ready && client_.waitStep(deadlineMs)) {
        ready = interfaceReady(iface, ip);
    }
    if (!ready) {
        nbt_logf("Wi-Fi Direct: interface %s is not ready", iface);
        out->clear();
        restore(currentMonotonic(nowMs));
        return false;
    }
    logWfdReady(cfg, &health_);
    memcpy(out->ssid, cfg.ssid, strlen(cfg.ssid) + 1);
    nbt_logf("Wi-Fi Direct ready on %s (%s): SSID %s (key from %s%s)", iface, ip, out->ssid,
             out->source, enabledByUs_ ? ", enabled by manager" : ", already active");
    return true;
}

void WifiDirectController::restore(uint64_t nowMs) {
    logWfdSessionEnd(&health_);
    if (!enabledByUs_) {
        return;
    }
    enabledByUs_ = false;
    if (androidAutoUsesWifiDirect()) {
        nbt_logf("Wi-Fi Direct: A0.03 group left on: Android Auto (NBT-AAD) uses it");
        return;
    }
    if (!client_.active() || !client_.linkUpWithTarget()) {
        return;
    }
    nbt_logf("Wi-Fi Direct: restoring A0.03 group to disabled");
    (void)client_.selectState(
        0, nowMs + configuredTimeout("NBT_TEST_WFD_RESTORE_MS", kRestoreTimeoutMs));
}

void WifiDirectController::shutdown() {
    if (client_.active()) {
        restore(currentMonotonic(0));
    }
    client_.close();
}

} // namespace worker
} // namespace nbt
