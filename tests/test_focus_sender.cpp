#include "nbt/gui/focus_sender.hpp"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

namespace {
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
unsigned fired[16];
unsigned firedCount = 0;
unsigned blockEvent = 0; // fire() waits while this event is blocked
unsigned stuckCalls = 0;
unsigned stuckElapsed[4];

uint64_t clockMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}

void fire(void *, unsigned event) {
    pthread_mutex_lock(&lock);
    if (firedCount < 16) {
        fired[firedCount++] = event;
    }
    while (blockEvent == event) {
        pthread_cond_wait(&changed, &lock);
    }
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

void stuck(void *, unsigned, unsigned elapsedMs) {
    pthread_mutex_lock(&lock);
    if (stuckCalls < 4) {
        stuckElapsed[stuckCalls] = elapsedMs;
    }
    ++stuckCalls;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

// Waits up to 2 s for a condition on the shared state.
bool waitUntil(bool (*done)()) {
    const uint64_t until = clockMs() + 2000;
    while (clockMs() < until) {
        pthread_mutex_lock(&lock);
        const bool ok = done();
        pthread_mutex_unlock(&lock);
        if (ok) {
            return true;
        }
        usleep(2000);
    }
    return false;
}

bool threeFired() {
    return firedCount == 3;
}
bool twoStuckCalls() {
    return stuckCalls == 2;
}

void reset(unsigned block) {
    pthread_mutex_lock(&lock);
    firedCount = 0;
    stuckCalls = 0;
    blockEvent = block;
    pthread_mutex_unlock(&lock);
}

void release() {
    pthread_mutex_lock(&lock);
    blockEvent = 0;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

// Events go out in order, each once while it waits, and the caller never waits.
void testOrderAndDuplicates() {
    nbt::gui::FocusSender sender;
    reset(52);
    CHECK(!sender.send(52)); // not started
    CHECK(sender.start(&fire, &stuck, &clockMs, 0, 200, 400));
    CHECK(sender.send(52)); // blocks in fire until released
    usleep(20000);
    CHECK(sender.send(50));
    CHECK(sender.send(50)); // already waiting: not queued twice
    CHECK(sender.send(51));
    uint64_t since = 0;
    CHECK(sender.current(since) == 52 && since);
    release();
    CHECK(waitUntil(&threeFired));
    CHECK(fired[0] == 52 && fired[1] == 50 && fired[2] == 51);
    usleep(20000);
    CHECK(sender.current(since) == 0 && !sender.stuck());
    pthread_mutex_lock(&lock);
    CHECK(stuckCalls == 0);
    pthread_mutex_unlock(&lock);
    sender.stop();
}

// An event iDrive does not answer: reported at both marks, later events
// dropped, and the sender recovers when it returns.
void testStuck() {
    nbt::gui::FocusSender sender;
    reset(50);
    CHECK(sender.start(&fire, &stuck, &clockMs, 0, 60, 150));
    CHECK(sender.send(50));
    CHECK(waitUntil(&twoStuckCalls));
    CHECK(stuckElapsed[0] >= 60 && stuckElapsed[0] < 150);
    CHECK(stuckElapsed[1] >= 150);
    CHECK(sender.stuck());
    CHECK(!sender.send(51));
    release();
    usleep(20000);
    CHECK(!sender.stuck());
    CHECK(sender.send(51));
    usleep(20000);
    pthread_mutex_lock(&lock);
    CHECK(firedCount == 2 && fired[1] == 51 && stuckCalls == 2);
    pthread_mutex_unlock(&lock);
    sender.stop();
}

// Stopping with the sender stuck in iDrive returns within its bound.
void testStopWhileStuck() {
    static nbt::gui::FocusSender sender; // its stuck thread outlives the test
    reset(50);
    CHECK(sender.start(&fire, &stuck, &clockMs, 0, 1000, 2000));
    CHECK(sender.send(50));
    usleep(20000);
    const uint64_t start = clockMs();
    sender.stop();
    CHECK(clockMs() - start < 1000);
    CHECK(!sender.send(51));
    release();
}
} // namespace

int main() {
    testOrderAndDuplicates();
    testStuck();
    testStopWhileStuck();
    usleep(20000);
    puts("focus sender tests passed");
    return 0;
}
