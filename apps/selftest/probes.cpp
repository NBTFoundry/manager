// SPDX-License-Identifier: MPL-2.0
#include "probes.hpp"

#include "nbt/app/files.hpp"
#include "nbt/app/log.hpp"

#include <memory>
#include <new>
#include <stdio.h>
#include <string.h>

namespace selftest {
namespace {

const uint64_t kCarDataMs = 5000;
const char *const kCarData[] = {"driving.speedActual", "sensors.temperatureExterior"};
const uint8_t kEngine = 0x12;
const uint16_t kActiveSession = 0xf186;

} // namespace

void checkBridge(const nbt::app::Lifecycle &lifecycle, const char *declared, Report &report) {
    char missing[Report::kDetailMax + 1] = "";
    unsigned count = 0, absent = 0;
    for (const char *cap = declared; *cap;) {
        const char *end = strchr(cap, ',');
        const size_t length = end ? static_cast<size_t>(end - cap) : strlen(cap);
        char name[32];
        if (length && length < sizeof(name)) {
            memcpy(name, cap, length);
            name[length] = 0;
            ++count;
            if (strcmp(name, "files") && !lifecycle.has_cap(name)) {
                const size_t used = strlen(missing);
                snprintf(missing + used, sizeof(missing) - used, "%s%s", absent++ ? "," : "", name);
            }
        }
        cap += length + (end ? 1 : 0);
    }
    if (absent) {
        report.set("bridge", kFail, "missing %s", missing);
    } else {
        report.set("bridge", kPass, "%u capabilities", count);
    }
}

void checkFiles(const nbt::app::Lifecycle &lifecycle, Report &report) {
    if (!lifecycle.has_cap("files") || !lifecycle.files_dir()[0]) {
        report.set("files", kSkip, "no files folder");
        return;
    }
    // About 104 KiB: not on the stack.
    std::unique_ptr<nbt::app::FileList> list(new (std::nothrow) nbt::app::FileList());
    if (!list) {
        report.set("files", kFail, "out of memory");
    } else if (!list->load(lifecycle.files_dir(), NULL)) {
        report.set("files", kFail, "cannot list the folder");
    } else {
        report.set("files", kPass, "%u files", static_cast<unsigned>(list->count()));
    }
}

void CarDataProbe::start(uint64_t nowMs, Report &report) {
    started_ = nowMs;
    if (!data_.init()) {
        report.set("cardata", kFail, "not granted");
        done_ = true;
    }
}

void CarDataProbe::poll(uint64_t nowMs, Report &report) {
    if (done_) {
        return;
    }
    for (size_t i = 0; i < sizeof(kCarData) / sizeof(kCarData[0]); ++i) {
        char value[201];
        if (data_.read(kCarData[i], value, sizeof(value), NULL)) {
            nbt::app::log_info("car data %s: %s", kCarData[i], value);
            report.set("cardata", kPass, "%s", strchr(kCarData[i], '.') + 1);
            done_ = true;
            return;
        }
    }
    if (nowMs - started_ >= kCarDataMs) {
        report.set("cardata", kSkip, "no value in 5 s");
        done_ = true;
    }
}

DiagProbe::DiagProbe()
    : thread_(), running_(false), reported_(false), finished_(0),
      status_(nbt::app::Diag::kNoChannel), nrc_(0) {
}

DiagProbe::~DiagProbe() {
    if (running_) {
        pthread_join(thread_, NULL);
    }
}

void *DiagProbe::run(void *self) {
    DiagProbe &probe = *static_cast<DiagProbe *>(self);
    uint8_t answer[16];
    size_t size = 0;
    probe.status_ =
        probe.diag_.read(kEngine, kActiveSession, answer, sizeof(answer), &size, &probe.nrc_);
    __atomic_store_n(&probe.finished_, 1, __ATOMIC_RELEASE);
    return NULL;
}

void DiagProbe::start(Report &report) {
    if (!diag_.init()) {
        report.set("diag", kFail, "not granted");
        reported_ = true;
        return;
    }
    running_ = pthread_create(&thread_, NULL, &DiagProbe::run, this) == 0;
    if (!running_) {
        report.set("diag", kFail, "no thread");
        reported_ = true;
    }
}

bool DiagProbe::poll(Report &report) {
    if (reported_) {
        return true;
    }
    if (!__atomic_load_n(&finished_, __ATOMIC_ACQUIRE)) {
        return false;
    }
    pthread_join(thread_, NULL);
    running_ = false;
    reported_ = true;
    typedef nbt::app::Diag D;
    switch (status_) {
    case D::kOk:
        if (nrc_) {
            report.set("diag", kPass, "DME answered (negative 0x%02x)", nrc_);
        } else {
            report.set("diag", kPass, "DME answered");
        }
        break;
    case D::kTimeout:
        report.set("diag", kSkip, "DME did not answer");
        break;
    case D::kNoRoute:
        report.set("diag", kSkip, "no gateway");
        break;
    case D::kBusy:
        report.set("diag", kSkip, "gateway busy");
        break;
    default:
        report.set("diag", kFail, "status %d", static_cast<int>(status_));
        break;
    }
    return true;
}

} // namespace selftest
