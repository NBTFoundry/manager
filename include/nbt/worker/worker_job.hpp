// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_WORKER_JOB_HPP
#define NBT_WORKER_WORKER_JOB_HPP

/* One slow task of the worker on its own thread: package verification, an
 * install's copy, SETUP. On the loop they stalled everything: the GUI pings
 * the worker every second with a 2 s timeout, and APPROVE waits 5 s, so a
 * large package could get the worker killed. The task gets a context only it
 * uses while it runs; the loop learns of its end through fd() and done().
 * One task at a time. Shared state is under mutex_. */

#include "nbt/core/mutex.hpp"

#include <pthread.h>
#include <sys/select.h>

namespace nbt {
namespace worker {

class WorkerJob {
public:
    typedef void (*Task)(void *context);

    WorkerJob();
    ~WorkerJob();
    WorkerJob(const WorkerJob &) = delete;
    WorkerJob &operator=(const WorkerJob &) = delete;

    bool initialize();
    /* False when a task is running or its thread does not start (errno). */
    bool start(Task task, void *context);
    /* Started and not yet collected by done(). */
    bool running() const;
    /* True once per task, when it has ended: its context is the loop's again. */
    bool done();
    void sets(fd_set *readers, int *highest) const;
    /* Waits for a running task to end (shutdown). */
    void wait();

private:
    enum Phase { Idle, Running, Ended };
    static void *run(void *self);

    mutable core::Mutex mutex_;
    pthread_t thread_;
    Phase phase_;
    Task task_;
    void *context_;
    int wake_[2];
};

} // namespace worker
} // namespace nbt

#endif
