#include "nbt/worker/worker_job.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace worker {

WorkerJob::WorkerJob() : mutex_(), thread_(), phase_(Idle), task_(NULL), context_(NULL) {
    wake_[0] = wake_[1] = -1;
}

WorkerJob::~WorkerJob() {
    wait();
    (void)done();
    for (int i = 0; i < 2; ++i) {
        if (wake_[i] >= 0) {
            close(wake_[i]);
        }
    }
}

bool WorkerJob::initialize() {
    if (wake_[0] >= 0) {
        return true;
    }
    if (pipe(wake_)) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        const int flags = fcntl(wake_[i], F_GETFL);
        (void)fcntl(wake_[i], F_SETFL, flags < 0 ? O_NONBLOCK : flags | O_NONBLOCK);
        (void)fcntl(wake_[i], F_SETFD, FD_CLOEXEC);
    }
    return true;
}

bool WorkerJob::start(Task task, void *context) {
    {
        core::LockGuard guard(mutex_);
        if (phase_ != Idle || !task) {
            errno = EBUSY;
            return false;
        }
        task_ = task;
        context_ = context;
        phase_ = Running;
    }
    // Signals stay with the worker's loop.
    sigset_t all, previous;
    sigfillset(&all);
    (void)pthread_sigmask(SIG_BLOCK, &all, &previous);
    const int error = pthread_create(&thread_, NULL, run, this);
    (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (error) {
        core::LockGuard guard(mutex_);
        phase_ = Idle;
        errno = error;
        return false;
    }
    return true;
}

void *WorkerJob::run(void *self) {
    WorkerJob &job = *static_cast<WorkerJob *>(self);
    Task task;
    void *context;
    {
        core::LockGuard guard(job.mutex_);
        task = job.task_;
        context = job.context_;
    }
    task(context);
    {
        core::LockGuard guard(job.mutex_);
        job.phase_ = Ended;
    }
    if (job.wake_[1] >= 0) {
        (void)!write(job.wake_[1], "w", 1);
    }
    return NULL;
}

bool WorkerJob::running() const {
    core::LockGuard guard(mutex_);
    return phase_ != Idle;
}

bool WorkerJob::done() {
    char drained[16];
    while (wake_[0] >= 0 && read(wake_[0], drained, sizeof(drained)) > 0) {
    }
    {
        core::LockGuard guard(mutex_);
        if (phase_ != Ended) {
            return false;
        }
        phase_ = Idle;
    }
    (void)pthread_join(thread_, NULL);
    return true;
}

void WorkerJob::sets(fd_set *readers, int *highest) const {
    core::LockGuard guard(mutex_);
    if (phase_ != Idle && wake_[0] >= 0) {
        FD_SET(wake_[0], readers);
        *highest = wake_[0] > *highest ? wake_[0] : *highest;
    }
}

void WorkerJob::wait() {
    for (;;) {
        {
            core::LockGuard guard(mutex_);
            if (phase_ != Running) {
                return;
            }
        }
        const struct timespec pause = {0, 20000000};
        nanosleep(&pause, NULL);
    }
}

} // namespace worker
} // namespace nbt
