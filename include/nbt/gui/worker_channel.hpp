// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_WORKER_CHANNEL_HPP
#define NBT_GUI_WORKER_CHANNEL_HPP

/* The GUI's link to the worker, on its own thread. A worker request blocks
 * until its reply or its timeout: seconds while the worker prepares Wi-Fi
 * Direct for Pair a phone, up to 120 s for SETUP. On the UI thread that froze
 * the screen and the controller and stopped the heartbeat the launcher needs
 * every 3 s (dev62 in the car: Pair a phone closed the manager).
 *
 * The UI thread submits one request at a time and takes its reply when it is
 * there (the controller ignores key presses meanwhile, as before); this thread
 * does the blocking I/O and, when idle for a second, asks the worker for its
 * list so the UI knows the worker is alive without waiting itself. Once
 * started, the WorkerClient is used by this thread only. Shared state is under
 * mutex_; each side wakes the other through a pipe (no timed condition
 * waits: they run on the wall clock, which the car sets while running). */

#include "nbt/core/mutex.hpp"
#include "nbt/core/status.hpp"
#include "nbt/gui/ui_controller.hpp"
#include "nbt/gui/worker_client.hpp"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace gui {

class WorkerChannel {
public:
    explicit WorkerChannel(WorkerClient &client);
    ~WorkerChannel();
    WorkerChannel(const WorkerChannel &) = delete;
    WorkerChannel &operator=(const WorkerChannel &) = delete;

    /* Starts the thread; the client is connected (HELLO done). */
    core::Status start();
    /* Stops and joins the thread; the client is then the caller's again. */
    void stop();

    /* False while a request is in flight or once the channel is closed. */
    bool submit(const Request &request);
    /* The finished request's reply, once: true when there is one. `ok` false:
     * it failed (timeout, closed channel) and the channel is now closed. */
    bool take(char *reply, size_t size, bool *ok, char *command, size_t commandSize);
    bool busy() const;
    bool connected() const;
    /* The worker has answered (the thread asks for its list at once), and
     * answered within the last 3 s or has a request in flight within its
     * deadline: the GUI's heartbeat may go on. */
    bool alive(uint64_t nowMs) const;
    /* Readable when a reply is ready; drained by take(). */
    int wakeFd() const {
        return toUi_[0];
    }

private:
    enum Phase { Idle, Pending, Running, Done };
    static void *run(void *self);
    void loop();
    bool ping(uint64_t nowMs);
    /* The connection is gone: a queued or running request fails, the UI wakes. */
    void lost();
    void wake(int fd);
    static void drain(int fd);

    WorkerClient &client_;
    mutable core::Mutex mutex_;
    pthread_t thread_;
    bool started_;
    bool stopping_;
    bool connected_;
    Phase phase_;
    Request request_;
    char reply_[kReplyMax];
    bool ok_;
    bool answered_; // the worker has answered a LIST: ready may be reported
    uint64_t lastOkMs_;
    uint64_t deadlineMs_;
    int toThread_[2];
    int toUi_[2];
};

} // namespace gui
} // namespace nbt

#endif
