#ifndef NBT_WORKER_WIFI_DIRECT_JOB_HPP
#define NBT_WORKER_WIFI_DIRECT_JOB_HPP

/* Pair a phone's Wi-Fi Direct preparation on its own thread.
 * WifiDirectController::prepareSession makes MOST round trips and waits for
 * wfd0 for up to 10 s; on the worker's loop that stalled the phone API, the
 * door and the GUI's requests (dev62 in the car: the GUI lost its heartbeat and
 * the manager closed). While a preparation runs, its thread alone uses the
 * controller: the loop's poll, sets and restore come through here and are
 * skipped, and the loop learns the result through wakeFd() and finish().
 * Shared state is under mutex_. */

#include "nbt/core/mutex.hpp"
#include "nbt/worker/wifi_direct_controller.hpp"

#include <pthread.h>
#include <stdint.h>
#include <sys/select.h>

namespace nbt {
namespace worker {

class WifiDirectJob {
public:
    explicit WifiDirectJob(WifiDirectController &wfd);
    ~WifiDirectJob();
    WifiDirectJob(const WifiDirectJob &) = delete;
    WifiDirectJob &operator=(const WifiDirectJob &) = delete;

    void initialize();
    /* True when preparing `iface` needs the controller (wfd0); otherwise a
     * session needs no preparation. */
    bool required(const char *iface) const;
    /* Starts preparing `iface`; false when one is running or no thread starts. */
    bool start(const char *iface, uint64_t nowMs);
    /* Once the preparation ended: true, with its outcome and credentials. */
    bool finish(bool *ok, ReadyWifiCredentials *out);
    bool running() const;

    /* The loop's use of the controller, skipped while a preparation runs. */
    int fd() const;
    void sets(fd_set *readers, int *highest) const;
    void poll(uint64_t nowMs);
    bool enabledByUs() const;
    void restore(uint64_t nowMs);
    /* Waits for a running preparation, then shuts the controller down. */
    void shutdown();

private:
    enum Phase { Idle, Running, Done };
    static void *run(void *self);
    void join();

    WifiDirectController &wfd_;
    mutable core::Mutex mutex_;
    pthread_t thread_;
    bool joinable_;
    Phase phase_;
    char iface_[32];
    uint64_t startMs_;
    bool ok_;
    ReadyWifiCredentials result_;
    int wake_[2];
};

} // namespace worker
} // namespace nbt

#endif
