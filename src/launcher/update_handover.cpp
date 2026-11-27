// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/update_handover.hpp"

#include "nbt/gui/hmi_hold.hpp"
#include "nbt/log.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace launcher {
namespace {

const unsigned kBeatMs = 250;
// The plugin polls every 250 ms and takes a line older than 1500 ms as gone.
const uint64_t kGapMs = 1000;

const char *livenessPath() {
#ifdef NBT_TESTING
    static char path[PATH_MAX];
    if (getenv("NBT_TEST_GUI_LIVENESS")) {
        return getenv("NBT_TEST_GUI_LIVENESS");
    }
    // The GUI's own test path (apps/manager/main.cpp) when the launcher starts a test GUI.
    const char *root = getenv("NBT_TEST_MANAGER_ROOT");
    if (getenv("NBT_TEST_MANAGER_GUI") && root &&
        snprintf(path, sizeof(path), "%s/gui-live", root) < static_cast<int>(sizeof(path))) {
        return path;
    }
#endif
    return gui::kHoldLivenessPath;
}

int lockByte1(int fd, short type) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    lock.l_start = 1;
    lock.l_len = 1;
    return fcntl(fd, F_SETLK, &lock);
}

uint64_t nowMs() {
    uint64_t now = 0;
    return nbt_monotonic_ms(&now) ? 0 : now;
}

} // namespace

UpdateHandover::UpdateHandover(const storage::ManagedRoot &root)
    : root_(root), livenessPath_(livenessPath()), fd_(-1), armed_(false), tried_(false),
      brokenLock_(false), thread_(), mutex_(), running_(false), stop_(false), gap_(false),
      lastBeatMs_(0) {
    pthread_mutex_init(&mutex_, NULL);
}

UpdateHandover::~UpdateHandover() {
    stopBeats();
    release();
    if (fd_ >= 0) {
        close(fd_);
    }
    pthread_mutex_destroy(&mutex_);
}

bool UpdateHandover::arm() {
    if (armed_ || tried_) {
        return armed_;
    }
    tried_ = true;
    if (fd_ < 0) {
        core::Result<core::UniqueFd> opened =
            root_.open(NBT_FS_MANAGER_LOCK, "manager", NULL, NBT_FS_OPEN_LOCK);
        if (!opened.ok()) {
            nbt_logf("update handover: gui.lock not opened (errno %d)", opened.status().code());
            return false;
        }
        fd_ = opened.value().release();
    }
    armed_ = lockByte1(fd_, F_WRLCK) == 0;
    if (!armed_) { // a GUI holding all of gui.lock: one from before 0.5.2
        nbt_logf("update handover: not available with this GUI (errno %d)", errno);
    }
    brokenLock_ = false;
    return armed_;
}

bool UpdateHandover::rearm() {
    if (!armed_) {
        return false;
    }
    if (lockByte1(fd_, F_WRLCK)) {
        brokenLock_ = true;
        nbt_logf("update handover: gui.lock not held again (errno %d)", errno);
        return false;
    }
    return true;
}

void UpdateHandover::release() {
    if (armed_ && fd_ >= 0) {
        (void)lockByte1(fd_, F_UNLCK);
    }
    armed_ = false;
    tried_ = false;
    brokenLock_ = false;
}

bool UpdateHandover::beat() {
    const uint64_t now = nowMs();
    pthread_mutex_lock(&mutex_);
    if (lastBeatMs_ && now - lastBeatMs_ > kGapMs) {
        gap_ = true;
    }
    lastBeatMs_ = now;
    pthread_mutex_unlock(&mutex_);
    return gui::holdLivenessWrite(livenessPath_, getpid(), now) == 0;
}

void *UpdateHandover::beatMain(void *self) {
    UpdateHandover *handover = static_cast<UpdateHandover *>(self);
    uint64_t next = nowMs() + kBeatMs;
    const struct timespec pause = {0, 50000000};
    for (;;) {
        pthread_mutex_lock(&handover->mutex_);
        const bool stop = handover->stop_;
        pthread_mutex_unlock(&handover->mutex_);
        if (stop) {
            return NULL;
        }
        const uint64_t now = nowMs();
        if (now >= next) {
            (void)handover->beat();
            next = now + kBeatMs;
        }
        nanosleep(&pause, NULL);
    }
}

void UpdateHandover::startBeats() {
    if (running_) {
        return;
    }
    // The old GUI handed the line to this process as it ended; a line that is
    // stale or names another process was already taken as the manager gone.
    gap_ = !gui::holdLivenessRead(livenessPath_, nowMs());
    lastBeatMs_ = 0;
    if (!beat()) {
        gap_ = true;
    }
    stop_ = false;
    running_ = pthread_create(&thread_, NULL, &UpdateHandover::beatMain, this) == 0;
    if (!running_) {
        gap_ = true;
    }
}

void UpdateHandover::stopBeats() {
    if (!running_) {
        return;
    }
    pthread_mutex_lock(&mutex_);
    stop_ = true;
    pthread_mutex_unlock(&mutex_);
    pthread_join(thread_, NULL);
    running_ = false;
    if (!beat()) {
        gap_ = true;
    }
}

bool UpdateHandover::continuous() const {
    pthread_mutex_lock(const_cast<pthread_mutex_t *>(&mutex_));
    const bool gap = gap_;
    pthread_mutex_unlock(const_cast<pthread_mutex_t *>(&mutex_));
    return armed_ && !brokenLock_ && !gap;
}

const char *UpdateHandover::broken() const {
    if (!armed_) {
        return "gui.lock byte 1 not held";
    }
    if (brokenLock_) {
        return "gui.lock byte 1 lost while staging";
    }
    return "a gap in the liveness line";
}

void UpdateHandover::abandon() {
    stopBeats();
    (void)unlink(livenessPath_);
    release();
    // Two plugin polls and more: it sees the manager gone before the new GUI
    // writes its own line, releases iDrive and opens the new GUI when chosen.
    const struct timespec pause = {1, 0};
    nanosleep(&pause, NULL);
}

} // namespace launcher
} // namespace nbt
