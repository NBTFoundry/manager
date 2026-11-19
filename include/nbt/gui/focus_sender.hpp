#ifndef NBT_GUI_FOCUS_SENDER_HPP
#define NBT_GUI_FOCUS_SENDER_HPP

// Sends iDrive focus events from a thread of the plugin's own, so that the
// plugin's iDrive thread (its bmw::Looper) never waits on iDrive.
//
// PluginApp::triggerEvent is synchronous: iDrive queues the event for its
// main thread and waits for the answer with no timeout (CLocalCommAdapter and
// CHmiGenMessenger in HmiMain3D; docs/car-issues-2026-11-17.md). On
// 2026-11-17 one never returned and iDrive froze. The sender keeps the plugin
// thread free and reports an event that has not returned after kFocusStuckMs;
// a watch thread then calls the stuck hook (the thread snapshot) at
// kFocusStuckMs and again at kFocusLateMs. Static storage, no heap.

#include <pthread.h>
#include <stdint.h>

namespace nbt {
namespace gui {

static const unsigned kFocusStuckMs = 3000;
static const unsigned kFocusLateMs = 10000;
static const unsigned kFocusQueue = 8;

typedef void (*FocusFire)(void *context, unsigned event);
typedef void (*FocusStuck)(void *context, unsigned event, unsigned elapsedMs);
typedef uint64_t (*FocusClock)();

class FocusSender {
public:
    FocusSender();
    // Starts the sender and watch threads; false if they cannot be created.
    bool start(FocusFire fire, FocusStuck onStuck, FocusClock clock, void *context,
               unsigned stuckMs = kFocusStuckMs, unsigned lateMs = kFocusLateMs);
    // Stops threads that are idle; a thread stuck in iDrive is left behind.
    void stop();
    // Queues an event; false when it is dropped (stuck, full, not started).
    // An event already waiting in the queue is not queued twice.
    bool send(unsigned event);
    // True while an event has been in iDrive for kFocusStuckMs or more.
    bool stuck();
    // The event in iDrive now (0 if none) and since when.
    unsigned current(uint64_t &sinceMs);

private:
    static void *senderMain(void *self);
    static void *watchMain(void *self);
    void senderLoop();
    void watchLoop();
    bool stuckLocked(uint64_t nowMs) const;

    pthread_mutex_t lock_;
    pthread_cond_t wake_;
    pthread_cond_t done_;
    FocusFire fire_;
    FocusStuck onStuck_;
    FocusClock clock_;
    void *context_;
    unsigned queue_[kFocusQueue];
    unsigned head_;
    unsigned count_;
    unsigned current_;
    uint64_t sinceMs_;
    uint64_t sequence_;
    unsigned stuckMs_;
    unsigned lateMs_;
    unsigned threads_;
    bool started_;
    bool stopping_;
};

} // namespace gui
} // namespace nbt

#endif
