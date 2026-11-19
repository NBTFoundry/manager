#include "nbt/log.h"
#include "nbt/log/internal.hpp"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int log_fd = -1;
static char name[16];
static nbt_log_rotate_fn rotate_fn;
static char log_path[NBT_PATH_MAX];
static unsigned file_number; /* +1 for each file this process starts writing to */

static int valid_component(const char *c) {
    size_t i;
    if (!c || !c[0]) {
        return 0;
    }
    for (i = 0; c[i]; ++i) {
        if (i >= sizeof(name) - 1 ||
            !((c[i] >= 'a' && c[i] <= 'z') || (c[i] >= 'A' && c[i] <= 'Z') ||
              (c[i] >= '0' && c[i] <= '9'))) {
            return 0;
        }
    }
    return 1;
}

int nbt_log_checked(int fd) {
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 || (st.st_mode & 0022)) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

/* Called with the lock held. */
static void stop_locked(void) {
    if (log_fd >= 0) {
        close(log_fd);
    }
    log_fd = -1;
    rotate_fn = NULL;
    log_path[0] = 0;
}

int nbt_log_attach(const char *component, const char *path, int fd, nbt_log_rotate_fn rotate) {
    if (fd < 0) {
        return -1;
    }
    if (!valid_component(component) || !path || strlen(path) >= sizeof(log_path)) {
        close(fd);
        return -1;
    }
    pthread_mutex_lock(&lock);
    stop_locked();
    log_fd = fd;
    ++file_number;
    memcpy(log_path, path, strlen(path) + 1);
    memcpy(name, component, strlen(component) + 1);
    rotate_fn = rotate;
    pthread_mutex_unlock(&lock);
    return 1;
}

/* An existing, checked file opened for appending, or -1. */
static int open_plain(const char *path) {
    int flags = O_WRONLY | O_APPEND, fd;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(path, flags);
    if (fd < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) || nbt_log_checked(fd)) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

/* The plugin never rotates: it follows a file another component rotated.
 * Called with the lock held. */
static int reopen_plain(int fd) {
    (void)fd;
    return open_plain(log_path);
}

int nbt_log_open_path(const char *path, const char *component) {
    int fd;
    if (!path || path[0] != '/' || strlen(path) >= sizeof(log_path) ||
        !valid_component(component)) {
        errno = EINVAL;
        return -1;
    }
    if (nbt_log_enabled()) {
        return 1;
    }
    fd = open_plain(path);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    return nbt_log_attach(component, path, fd, reopen_plain);
}

int nbt_log_enabled(void) {
    int on;
    pthread_mutex_lock(&lock);
    on = log_fd >= 0;
    pthread_mutex_unlock(&lock);
    return on;
}

static long long size_of(int fd) {
    struct stat st;
    return fstat(fd, &st) ? -1 : (long long)st.st_size;
}

/* 1 when `fd` is no longer the file at the log path (rotated or removed). */
static int moved(int fd) {
    struct stat a, b;
    if (fstat(fd, &a) || lstat(log_path, &b)) {
        return 1;
    }
    return a.st_dev != b.st_dev || a.st_ino != b.st_ino;
}

unsigned nbt_log_file(void) {
    unsigned number;
    pthread_mutex_lock(&lock);
    /* Follow a file replaced since the last line (switched off and on). */
    if (log_fd >= 0 && moved(log_fd) && rotate_fn) {
        const int next = rotate_fn(log_fd);
        if (next >= 0) {
            close(log_fd);
            log_fd = next;
            ++file_number;
        }
    }
    number = log_fd >= 0 ? file_number : 0;
    pthread_mutex_unlock(&lock);
    return number;
}

void nbt_logf(const char *format, ...) {
    char message[NBT_LOG_LINE + 1], line[NBT_LOG_LINE + 64];
    struct timespec now;
    va_list args;
    long long size;
    size_t i, length;
    int n;
    if (!format || !nbt_log_enabled()) {
        return;
    }
    va_start(args, format);
    n = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    for (i = 0; message[i]; ++i) {
        if ((unsigned char)message[i] < 0x20 || (unsigned char)message[i] > 0x7e) {
            message[i] = '?';
        }
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now)) {
        memset(&now, 0, sizeof(now));
    }
    pthread_mutex_lock(&lock);
    if (log_fd < 0) {
        pthread_mutex_unlock(&lock);
        return;
    }
    n = snprintf(line, sizeof(line), "%llu.%03u %s[%ld] %s\n", (unsigned long long)now.tv_sec,
                 (unsigned)(now.tv_nsec / 1000000), name, (long)getpid(), message);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        pthread_mutex_unlock(&lock);
        return;
    }
    length = (size_t)n;
    size = size_of(log_fd);
    /* Another process rotated the file, or this one is full. */
    if (size < 0 || (unsigned long long)size + length > NBT_LOG_MAX || moved(log_fd)) {
        int next = rotate_fn ? rotate_fn(log_fd) : -1;
        if (next >= 0) {
            close(log_fd);
            log_fd = next;
            ++file_number;
            size = size_of(log_fd);
        } else {
            struct stat st;
            /* Removing the log file switches logging off. */
            if (lstat(log_path, &st) && errno == ENOENT) {
                stop_locked();
                pthread_mutex_unlock(&lock);
                return;
            }
        }
    }
    /* A full file (or one that cannot be rotated) drops the line. One write
     * per line keeps lines from different processes whole; a short write on a
     * full disk leaves at most one partial line. */
    if (size >= 0 && (unsigned long long)size + length <= NBT_LOG_MAX) {
        ssize_t written;
        do {
            written = write(log_fd, line, length);
        } while (written < 0 && errno == EINTR);
    }
    pthread_mutex_unlock(&lock);
}

void nbt_log_close(void) {
    pthread_mutex_lock(&lock);
    stop_locked();
    pthread_mutex_unlock(&lock);
}
