#include "nbt/gui/focus_sender.hpp"

#include <time.h>

namespace nbt {
namespace gui {

namespace {
const unsigned kStopWaitMs = 500;

// pthread_cond_timedwait takes CLOCK_REALTIME; wait for at most ms from now.
void waitFor(pthread_cond_t *cond, pthread_mutex_t *lock, uint64_t ms) {
    struct timespec at;
    clock_gettime(CLOCK_REALTIME, &at);
    at.tv_sec += static_cast<time_t>(ms / 1000u);
    at.tv_nsec += static_cast<long>(ms % 1000u) * 1000000L;
    if (at.tv_nsec >= 1000000000L) {
        at.tv_sec += 1;
        at.tv_nsec -= 1000000000L;
    }
    (void)pthread_cond_timedwait(cond, lock, &at);
}
} // namespace

FocusSender::FocusSender()
    : fire_(0), onStuck_(0), clock_(0), context_(0), head_(0), count_(0), current_(0), sinceMs_(0),
      sequence_(0), stuckMs_(kFocusStuckMs), lateMs_(kFocusLateMs), threads_(0), started_(false),
      stopping_(false) {
    pthread_mutex_init(&lock_, 0);
    pthread_cond_init(&wake_, 0);
    pthread_cond_init(&done_, 0);
}

bool FocusSender::start(FocusFire fire, FocusStuck onStuck, FocusClock clock, void *context,
                        unsigned stuckMs, unsigned lateMs) {
    pthread_mutex_lock(&lock_);
    if (started_) {
        pthread_mutex_unlock(&lock_);
        return true;
    }
    fire_ = fire;
    onStuck_ = onStuck;
    clock_ = clock;
    context_ = context;
    stuckMs_ = stuckMs;
    lateMs_ = lateMs > stuckMs ? lateMs : stuckMs + 1;
    stopping_ = false;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t sender, watch;
    if (!pthread_create(&sender, &attr, &FocusSender::senderMain, this)) {
        ++threads_;
        if (!pthread_create(&watch, &attr, &FocusSender::watchMain, this)) {
            ++threads_;
            started_ = true;
        } else {
            stopping_ = true; // the sender exits at once
            pthread_cond_broadcast(&wake_);
        }
    }
    pthread_attr_destroy(&attr);
    const bool ok = started_;
    pthread_mutex_unlock(&lock_);
    return ok;
}

void FocusSender::stop() {
    pthread_mutex_lock(&lock_);
    stopping_ = true;
    count_ = 0;
    pthread_cond_broadcast(&wake_);
    pthread_cond_broadcast(&done_);
    const uint64_t until = clock_ ? clock_() + kStopWaitMs : 0;
    while (threads_ && clock_ && clock_() < until) {
        waitFor(&done_, &lock_, until - clock_());
    }
    started_ = false;
    pthread_mutex_unlock(&lock_);
}

bool FocusSender::stuckLocked(uint64_t nowMs) const {
    return current_ && nowMs >= sinceMs_ && nowMs - sinceMs_ >= stuckMs_;
}

bool FocusSender::send(unsigned event) {
    pthread_mutex_lock(&lock_);
    bool queued = false;
    if (started_ && !stopping_ && !stuckLocked(clock_()) && count_ < kFocusQueue) {
        queued = true;
        for (unsigned i = 0; i < count_; ++i) {
            if (queue_[(head_ + i) % kFocusQueue] == event) {
                pthread_mutex_unlock(&lock_);
                return true; // already waiting
            }
        }
        queue_[(head_ + count_) % kFocusQueue] = event;
        ++count_;
        pthread_cond_broadcast(&wake_);
    }
    pthread_mutex_unlock(&lock_);
    return queued;
}

bool FocusSender::stuck() {
    pthread_mutex_lock(&lock_);
    const bool result = started_ && stuckLocked(clock_());
    pthread_mutex_unlock(&lock_);
    return result;
}

unsigned FocusSender::current(uint64_t &sinceMs) {
    pthread_mutex_lock(&lock_);
    const unsigned event = current_;
    sinceMs = sinceMs_;
    pthread_mutex_unlock(&lock_);
    return event;
}

void *FocusSender::senderMain(void *self) {
    static_cast<FocusSender *>(self)->senderLoop();
    return 0;
}

void *FocusSender::watchMain(void *self) {
    static_cast<FocusSender *>(self)->watchLoop();
    return 0;
}

void FocusSender::senderLoop() {
    pthread_mutex_lock(&lock_);
    while (!stopping_) {
        if (!count_) {
            pthread_cond_wait(&wake_, &lock_);
            continue;
        }
        const unsigned event = queue_[head_];
        head_ = (head_ + 1) % kFocusQueue;
        --count_;
        current_ = event;
        sinceMs_ = clock_();
        ++sequence_;
        pthread_cond_broadcast(&done_);
        pthread_mutex_unlock(&lock_);
        fire_(context_, event); // may never return: iDrive is stuck
        pthread_mutex_lock(&lock_);
        current_ = 0;
        pthread_cond_broadcast(&done_);
    }
    --threads_;
    pthread_cond_broadcast(&done_);
    pthread_mutex_unlock(&lock_);
}

void FocusSender::watchLoop() {
    pthread_mutex_lock(&lock_);
    while (!stopping_) {
        if (!current_) {
            pthread_cond_wait(&done_, &lock_);
            continue;
        }
        const uint64_t sequence = sequence_, since = sinceMs_;
        const unsigned event = current_;
        const unsigned marks[2] = {stuckMs_, lateMs_};
        for (unsigned i = 0; i < 2; ++i) {
            while (!stopping_ && current_ && sequence_ == sequence && clock_() < since + marks[i]) {
                waitFor(&done_, &lock_, since + marks[i] - clock_());
            }
            if (stopping_ || !current_ || sequence_ != sequence) {
                break;
            }
            const unsigned elapsed = static_cast<unsigned>(clock_() - since);
            pthread_mutex_unlock(&lock_);
            onStuck_(context_, event, elapsed);
            pthread_mutex_lock(&lock_);
        }
        while (!stopping_ && current_ && sequence_ == sequence) {
            pthread_cond_wait(&done_, &lock_);
        }
    }
    --threads_;
    pthread_cond_broadcast(&done_);
    pthread_mutex_unlock(&lock_);
}

} // namespace gui
} // namespace nbt
