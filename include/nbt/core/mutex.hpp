// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CORE_MUTEX_HPP
#define NBT_CORE_MUTEX_HPP

#include "nbt/core/check.hpp"

#include <pthread.h>

namespace nbt {
namespace core {

class StaticMutex {
public:
    void lock() {
        NBT_CHECK(pthread_mutex_lock(&mutex_) == 0);
    }
    void unlock() {
        NBT_CHECK(pthread_mutex_unlock(&mutex_) == 0);
    }
    pthread_mutex_t mutex_;
};

#ifdef __QNXNTO__
#define NBT_STATIC_MUTEX_INITIALIZER                                                               \
    {                                                                                              \
        {                                                                                          \
            (int)_NTO_SYNC_NONRECURSIVE, (unsigned)_NTO_SYNC_INITIALIZER                           \
        }                                                                                          \
    }
#else
#define NBT_STATIC_MUTEX_INITIALIZER {PTHREAD_MUTEX_INITIALIZER}
#endif

class Mutex {
public:
    Mutex() {
        NBT_CHECK(pthread_mutex_init(&mutex_, NULL) == 0);
    }
    ~Mutex() {
        NBT_CHECK(pthread_mutex_destroy(&mutex_) == 0);
    }
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;
    void lock() {
        NBT_CHECK(pthread_mutex_lock(&mutex_) == 0);
    }
    void unlock() {
        NBT_CHECK(pthread_mutex_unlock(&mutex_) == 0);
    }

private:
    pthread_mutex_t mutex_;
};

class LockGuard {
public:
    explicit LockGuard(StaticMutex &mutex) : staticMutex_(&mutex), mutex_(NULL) {
        mutex.lock();
    }
    explicit LockGuard(Mutex &mutex) : staticMutex_(NULL), mutex_(&mutex) {
        mutex.lock();
    }
    ~LockGuard() {
        if (staticMutex_) {
            staticMutex_->unlock();
        } else {
            mutex_->unlock();
        }
    }
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    StaticMutex *staticMutex_;
    Mutex *mutex_;
};

} // namespace core
} // namespace nbt

#endif
