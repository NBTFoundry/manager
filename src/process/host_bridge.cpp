// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/host_bridge.hpp"

#include "nbt/app_host.h"
#include "nbt/firewall.h"
#include "nbt/gui/car_data_relay.hpp"
#include "nbt/gui/hmi_hold.hpp"
#include "nbt/log.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

const char *const kWords[] = {"", "hold", "peek", "release"};

void removeRecord(const char *dir, const char *app, const char *suffix) {
    char path[NBT_PATH_MAX];
    if (dir && !gui::appRecordPath(dir, app, suffix, path, sizeof(path))) {
        (void)unlink(path);
    }
}

} // namespace

HostBridge::HostBridge()
    : fs_(NULL), app_(), dir_(NULL), holdFd_(-1), carData_(false), netOpen_(false),
      haveRequest_(false), relayed_(false), cds_(), net_(), requestSequence_(0), relayMs_(0),
      recordMs_(0), requestWord_(NBT_HOLD_RELEASE) {
}

HostBridge::~HostBridge() {
    stop();
}

void HostBridge::prepare(nbt_fs *fs, const char *app, const char *grantCaps,
                         const nbt_bridge_requirement &need, nbt_app_environment *environment) {
    fs_ = fs;
    dir_ = nbt_fs_bridge_dir(fs);
    if (!app || strlen(app) > NBT_HOLD_APP_MAX || !dir_ || !grantCaps) {
        return;
    }
    memcpy(app_, app, strlen(app) + 1);
    if (nbt_bridge_cap_has(grantCaps, "hold")) {
        holdFd_ = nbt_fs_bridge_open(fs, app, "hold", 1);
        if (holdFd_ < 0) {
            nbt_logf("host %s: no hold channel (errno %d); started without", app, errno);
            char path[NBT_PATH_MAX], entry[NBT_PATH_MAX + 16];
            nbt_app_environment_remove(environment, "NBT_HOLD_FD=9");
            if (!gui::appHoldAnswerPath(dir_, app, path, sizeof(path))) {
                snprintf(entry, sizeof(entry), "NBT_HOLD_STATE=%s", path);
                nbt_app_environment_remove(environment, entry);
            }
        }
    }
    carData_ = need.cds[0] && nbt_bridge_cap_has(grantCaps, "car-data");
    if (carData_) {
        memcpy(cds_, need.cds, sizeof(cds_));
    }
    nbt_net_policy policy;
    if (need.net[0] && nbt_bridge_cap_has(grantCaps, "net") &&
        !nbt_net_policy_parse(need.net, &policy)) {
        (void)nbt_firewall_app_close(app); // a crashed host's rules
        bool opened = true;
        for (unsigned i = 0; opened && i < policy.count; ++i) {
            for (unsigned bit = NBT_NET_UAP0; opened && bit <= NBT_NET_WFD0; bit <<= 1) {
                if ((policy.rules[i].interfaces & bit) &&
                    nbt_firewall_app_add(app, nbt_net_interface_name(bit), policy.rules[i].port)) {
                    nbt_logf("host %s: packet filter rule TCP %u on %s refused (errno %d)", app,
                             static_cast<unsigned>(policy.rules[i].port),
                             nbt_net_interface_name(bit), errno);
                    opened = false;
                }
            }
        }
        if (opened) {
            netOpen_ = true;
            memcpy(net_, need.net, sizeof(net_));
            nbt_logf("host %s: packet filter open for %s", app, net_);
        } else {
            (void)nbt_firewall_app_close(app);
            char entry[NBT_NET_POLICY_MAX + 16];
            snprintf(entry, sizeof(entry), "NBT_NET=%s", need.net);
            nbt_app_environment_remove(environment, entry);
        }
    }
}

void HostBridge::relay(uint64_t now, nbt_hold_word word) {
    char path[NBT_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/nbt-bridge1-hmi", dir_) >=
            static_cast<int>(sizeof(path)) ||
        gui::appHoldRelayWrite(path, now, requestSequence_, app_, word)) {
        nbt_logf("host %s: cannot relay its hold (errno %d)", app_, errno);
    }
    relayMs_ = now;
}

void HostBridge::poll(uint64_t now, bool ready) {
    if (holdFd_ >= 0 && ready) {
        char line[NBT_HOLD_LINE_MAX + 1];
        uint64_t sequence = 0;
        nbt_hold_word word = NBT_HOLD_RELEASE;
        const ssize_t n = pread(holdFd_, line, sizeof(line), 0);
        bool changed = false;
        if (n > 0 && !nbt_hold_request_parse(line, static_cast<size_t>(n), &sequence, &word) &&
            (!haveRequest_ || sequence != requestSequence_ || word != requestWord_)) {
            haveRequest_ = changed = true;
            requestSequence_ = sequence;
            requestWord_ = word;
            nbt_logf("host %s: hold request %s", app_, kWords[word]);
        }
        if (haveRequest_ && requestWord_ != NBT_HOLD_RELEASE &&
            (changed || now - relayMs_ >= gui::kCarDataRefreshMs)) {
            relay(now, requestWord_);
            relayed_ = true;
        } else if (haveRequest_ && requestWord_ == NBT_HOLD_RELEASE && changed && relayed_) {
            relay(now, NBT_HOLD_RELEASE);
            relayed_ = false;
        }
    }
    if ((carData_ || netOpen_) && (!recordMs_ || now - recordMs_ >= gui::kCarDataRefreshMs)) {
        recordMs_ = now;
        if (carData_ && gui::carDataWantWrite(dir_, app_, now, cds_)) {
            nbt_logf("host %s: cannot publish its car data names (errno %d)", app_, errno);
        }
        if (netOpen_ && gui::netRecordWrite(dir_, app_, now, net_)) {
            nbt_logf("host %s: cannot publish its network rules (errno %d)", app_, errno);
        }
    }
}

void HostBridge::stop() {
    if (!app_[0]) {
        return;
    }
    if (relayed_) {
        uint64_t now = 0;
        (void)nbt_monotonic_ms(&now);
        relay(now, NBT_HOLD_RELEASE);
        relayed_ = false;
    }
    if (holdFd_ >= 0) {
        close(holdFd_);
        holdFd_ = -1;
    }
    if (carData_) {
        removeRecord(dir_, app_, "want");
        carData_ = false;
    }
    if (netOpen_) {
        removeRecord(dir_, app_, "net");
        if (nbt_firewall_app_close(app_) < 0) {
            nbt_logf("host %s: packet filter rules not removed (errno %d)", app_, errno);
        }
        netOpen_ = false;
    }
    app_[0] = 0;
}

} // namespace process
} // namespace nbt
