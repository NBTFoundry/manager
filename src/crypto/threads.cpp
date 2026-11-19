// SPDX-License-Identifier: MPL-2.0
#include "nbt/crypto/threads.hpp"

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#include <pthread.h>
#include <stdlib.h>
#endif

namespace nbt {
namespace crypto {

#if OPENSSL_VERSION_NUMBER < 0x10100000L
namespace {

pthread_mutex_t *locks;

void lockingCallback(int mode, int type, const char *file, int line) {
    (void)file;
    (void)line;
    if (type < 0 || type >= CRYPTO_num_locks()) {
        return;
    }
    if (mode & CRYPTO_LOCK) {
        (void)pthread_mutex_lock(&locks[type]);
    } else {
        (void)pthread_mutex_unlock(&locks[type]);
    }
}

unsigned long threadId() {
    return static_cast<unsigned long>(pthread_self());
}

} // namespace

bool initializeThreads() {
    if (locks) {
        return true;
    }
    const int count = CRYPTO_num_locks();
    if (count <= 0) {
        return false;
    }
    pthread_mutex_t *created =
        static_cast<pthread_mutex_t *>(calloc(static_cast<size_t>(count), sizeof(*created)));
    if (!created) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (pthread_mutex_init(&created[i], NULL)) {
            while (i-- > 0) {
                (void)pthread_mutex_destroy(&created[i]);
            }
            free(created);
            return false;
        }
    }
    locks = created; // for the process's life
    CRYPTO_set_id_callback(threadId);
    CRYPTO_set_locking_callback(lockingCallback);
    return true;
}

#else

bool initializeThreads() {
    return true;
}

#endif

} // namespace crypto
} // namespace nbt
