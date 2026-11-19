// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/wfd_health.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/log.h"
#include "nbt/worker/wifi_direct.hpp"

#include <errno.h>
#include <memory>
#include <new>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

const char kPidFile[] = "/tmp/dhcpd.pid.wfd0";
const char kConfFile[] = "/var/opt/conn/dhcpd.wfd0.conf";
const char kLeaseFile[] = "/var/opt/conn/dhcpd.wfd0.leases";
const size_t kLeaseMax = 65536;

// "running", "not running", or "no pid file".
const char *dhcpdState() {
    char text[16];
    size_t length = 0;
    if (!readCarFile(kPidFile, text, sizeof(text) - 1, &length)) {
        return "no pid file";
    }
    text[length] = 0;
    char *end = NULL;
    const long pid = strtol(text, &end, 10);
    if (end == text || pid <= 0) {
        return "no pid in its file";
    }
    return !kill(static_cast<pid_t>(pid), 0) || errno == EPERM ? "running" : "not running";
}

int leaseCount() {
    std::unique_ptr<char[]> text(new (std::nothrow) char[kLeaseMax]);
    size_t length = 0;
    if (!text || !readCarFile(kLeaseFile, text.get(), kLeaseMax, &length)) {
        return -1;
    }
    const unsigned count = countDhcpLeases(text.get(), length);
    core::secureClear(text.get(), kLeaseMax);
    return static_cast<int>(count);
}

} // namespace

unsigned countDhcpLeases(const char *text, size_t size) {
    unsigned count = 0;
    bool lineStart = true;
    for (size_t i = 0; i < size; ++i) {
        if (lineStart && size - i >= 6 && !memcmp(text + i, "lease ", 6)) {
            ++count;
        }
        lineStart = text[i] == '\n';
    }
    return count;
}

void logWfdReady(const WfdConfig &cfg, WfdHealth *health) {
    const char *key = "MOST gave no passphrase";
    if (cfg.passphrase.size()) {
        char saved[kWifiPassMax + 1] = {0};
        key = !readSavedWfdPassphrase(saved)          ? "WFDPass.conf unreadable"
              : !strcmp(saved, cfg.passphrase.data()) ? "same as WFDPass.conf"
                                                      : "differs from WFDPass.conf";
        core::secureClear(saved, sizeof(saved));
    }
    health->leasesAtStart = leaseCount();
    health->active = true;
    char leases[16] = "no file";
    if (health->leasesAtStart >= 0) {
        snprintf(leases, sizeof(leases), "%d", health->leasesAtStart);
    }
    nbt_logf("Wi-Fi Direct check: MOST passphrase %s; dhcpd for wfd0 %s; its config %s; "
             "leases %s",
             key, dhcpdState(), carFileExists(kConfFile) ? "present" : "missing", leases);
}

void logWfdSessionEnd(WfdHealth *health) {
    if (!health->active) {
        return;
    }
    health->active = false;
    const int now = leaseCount();
    nbt_logf("Wi-Fi Direct check at session end: dhcpd for wfd0 %s; leases %d (%d at start)",
             dhcpdState(), now, health->leasesAtStart);
}

} // namespace worker
} // namespace nbt
