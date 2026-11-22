// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/install_check_log.hpp"

#include <string.h>
#include <unistd.h>

namespace nbt {
namespace worker {

InstallCheckLog::~InstallCheckLog() {
    end();
}

int InstallCheckLog::begin(nbt_fs *fs, const char *app) {
    end();
    if (!app || strlen(app) >= sizeof(app_)) {
        return -1;
    }
    memcpy(app_, app, strlen(app) + 1);
    output_.retarget(fs, app_);
    fs_ = fs;
    active_ = true;
    write_ = output_.open();
    return write_;
}

void InstallCheckLog::closeWrite() {
    if (write_ >= 0) {
        close(write_);
        write_ = -1;
    }
}

void InstallCheckLog::started(const char *version, uint64_t sequence,
                              const process::ChildProcess &child) {
    closeWrite(); // the candidate has its own copy: end of file once it ends
    if (active_) {
        output_.beginRun(version, sequence, static_cast<long>(child.pid()), "install check");
        output_.log().note("install check of %s started (pid %ld)", version,
                           static_cast<long>(child.pid()));
    }
}

void InstallCheckLog::notStarted(const char *version, int error) {
    closeWrite();
    if (active_) {
        output_.log().note("install check of %s: cannot start (error %d)", version, error);
    }
    end();
}

void InstallCheckLog::drain() {
    if (active_) {
        output_.drain();
    }
}

void InstallCheckLog::passed() {
    if (active_) {
        output_.drain();
        output_.log().note("install check passed");
    }
}

void InstallCheckLog::failed(const process::ChildProcess &child, bool timedOut) {
    if (!active_) {
        return;
    }
    // Ended (reaped: its exit or signal), or still running with a broken
    // contract (fault), or still running when the check gave up.
    if (!child.running() || child.legacy().fault) {
        if (output_.ended(child.legacy())) {
            (void)output_.report(fs_, app_, false); // no sloginfo: the worker would wait
        }
    } else {
        output_.drain();
        if (timedOut) {
            output_.log().note("not healthy within 45 s");
        }
    }
    output_.log().note("install check failed");
}

void InstallCheckLog::end() {
    closeWrite();
    if (active_) {
        output_.drain();
        output_.retarget(NULL, "");
        active_ = false;
    }
}

} // namespace worker
} // namespace nbt
