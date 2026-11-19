// SPDX-License-Identifier: MPL-2.0
/* Passive bridge recorder for the bench procedure. Captures can contain
 * location/VIN data; keep stdout outside the source tree. */
#include "nbt/core/unique_fd.hpp"
#include "nbt/most_wire.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t stopping;

void stop(int) {
    stopping = 1;
}

int descriptor(const char *text) {
    char *end = NULL;
    errno = 0;
    const long value = strtol(text, &end, 10);
    return !errno && end != text && !*end && value >= 3 && value < FD_SETSIZE
               ? static_cast<int>(value)
               : -1;
}

bool clockMs(uint64_t *out) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time)) {
        return false;
    }
    *out =
        static_cast<uint64_t>(time.tv_sec) * 1000u + static_cast<uint64_t>(time.tv_nsec) / 1000000u;
    return true;
}

bool captureOutput() {
    const char *directory = getenv("NBT_DATA_DIR");
    char path[PATH_MAX];
    if (!directory) {
        return false;
    }
    const int n = snprintf(path, sizeof(path), "%s/most-capture.log", directory);
    if (n < 0 || n >= (int)sizeof(path)) {
        return false;
    }
    struct stat before, after, named;
    int flags = O_WRONLY | O_APPEND;
    const bool exists = !lstat(path, &before);
    if (exists ? !S_ISREG(before.st_mode) : errno != ENOENT) {
        return false;
    }
    if (!exists) {
        flags |= O_CREAT | O_EXCL;
    }
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    nbt::core::UniqueFd fd(open(path, flags, 0600));
    return fd.get() >= 0 && !fstat(fd.get(), &after) && !lstat(path, &named) &&
           S_ISREG(named.st_mode) && S_ISREG(after.st_mode) && after.st_nlink == 1 &&
           after.st_uid == geteuid() && !(after.st_mode & 077) && after.st_dev == named.st_dev &&
           after.st_ino == named.st_ino &&
           (!exists || (before.st_dev == after.st_dev && before.st_ino == after.st_ino)) &&
           dup2(fd.get(), 1) == 1;
}

class Recorder {
public:
    explicit Recorder(const char *get) : up_(false), present_(false), sent_(false), function_(0) {
        if (get && !strcmp(get, "speed")) {
            function_ = 0x621;
        } else if (get && !strcmp(get, "gear")) {
            function_ = 0x228;
        }
    }

    bool record(int fd, const uint8_t *bytes, size_t size, uint64_t now) {
        const int type = nbt_most_wire_type(bytes, size);
        if (type == NBT_MOST_WIRE_STATE) {
            nbt_most_wire_state state;
            if (nbt_most_wire_decode_state(bytes, size, &state)) {
                return false;
            }
            printf("%llu STATE %u %04X dropped=%lu\n", (unsigned long long)now,
                   (unsigned)state.link, (unsigned)state.address, (unsigned long)state.dropped);
            up_ = state.link == NBT_MOST_LINK_UP;
            if (state.link == NBT_MOST_LINK_DETACHED) {
                present_ = false;
            }
        } else if (type == NBT_MOST_WIRE_REGISTRY) {
            nbt_most_wire_entry entries[NBT_MOST_WIRE_REGISTRY_MAX];
            const long count =
                nbt_most_wire_decode_registry(bytes, size, entries, NBT_MOST_WIRE_REGISTRY_MAX);
            if (count < 0) {
                return false;
            }
            printf("%llu REGISTRY %ld\n", (unsigned long long)now, count);
            present_ = false;
            for (long i = 0; i < count; ++i) {
                printf("%llu NODE %04X %02X.%02X\n", (unsigned long long)now,
                       (unsigned)entries[i].address, entries[i].key >> 8, entries[i].key & 255);
                if (entries[i].key == 0x0500 && entries[i].address >= 0x100 &&
                    entries[i].address <= 0xfff) {
                    present_ = true;
                }
            }
        } else if (type == NBT_MOST_WIRE_MESSAGE) {
            nbt_most_wire_message message;
            if (nbt_most_wire_decode_message(bytes, size, &message)) {
                return false;
            }
            printf("%llu MESSAGE %04X>%04X %02X.%02X.%03X op=%X flags=%u tag=%lu dropped=%lu ",
                   (unsigned long long)now, (unsigned)message.src, (unsigned)message.dst,
                   (unsigned)message.fblock, (unsigned)message.inst, (unsigned)message.fkt,
                   (unsigned)message.op, (unsigned)message.flags, (unsigned long)message.tag,
                   (unsigned long)message.dropped);
            for (size_t i = 0; i < message.length; ++i) {
                printf("%02X", (unsigned)message.data[i]);
            }
            putchar('\n');
        } else if (type == NBT_MOST_WIRE_RESULT) {
            nbt_most_wire_result result;
            if (nbt_most_wire_decode_result(bytes, size, &result)) {
                return false;
            }
            printf("%llu RESULT %lu %u\n", (unsigned long long)now, (unsigned long)result.request,
                   (unsigned)result.code);
        } else {
            return false;
        }
        if (function_ && up_ && present_ && !sent_) {
            uint8_t out[NBT_MOST_WIRE_HEADER];
            nbt_most_wire_message message;
            memset(&message, 0, sizeof(message));
            message.fblock = 5;
            message.fkt = function_;
            message.op = 1;
            message.request = 1;
            const long n =
                nbt_most_wire_encode_message(NBT_MOST_WIRE_SEND, &message, out, sizeof(out));
            if (n < 0 || send(fd, out, (size_t)n, 0) != n) {
                return false;
            }
            sent_ = true;
            printf("%llu GET 05.00.%03X request=1\n", (unsigned long long)now, (unsigned)function_);
        }
        return fflush(stdout) == 0;
    }

private:
    bool up_, present_, sent_;
    uint16_t function_;
};
} // namespace

int main(int argc, char **argv) {
    int ready = -1, lease = -1;
    if (argc == 5 && !strcmp(argv[1], "--ready-fd") && !strcmp(argv[3], "--lease-fd")) {
        ready = descriptor(argv[2]);
        lease = descriptor(argv[4]);
        if (ready < 0 || lease < 0 || ready == lease || ready == 6 || lease == 6) {
            return 2;
        }
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_SET;
        if (fcntl(lease, F_SETLK, &lock)) {
            return 1;
        }
    } else if (argc != 1) {
        return 2;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) {
        return 1;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL)) {
        return 1;
    }
    const char *launch = getenv("NBT_LAUNCH");
    const bool health = launch && !strcmp(launch, "check");
    const bool capture = ready >= 0 && !health;
    if (capture && !captureOutput()) {
        return 1;
    }
    const char *most = getenv("NBT_MOST_FD");
    if (!health && (!most || strcmp(most, "6"))) {
        fprintf(stderr, "MOST capability is unavailable\n");
        return 1;
    }
    const char *get = getenv("NBT_SETTING_GET");
    if (get && strcmp(get, "off") && strcmp(get, "speed") && strcmp(get, "gear")) {
        return 2;
    }
    Recorder recorder(get);
    uint64_t heartbeat = 0;
    while (!stopping) {
        uint64_t now = 0;
        if (!clockMs(&now)) {
            return 1;
        }
        if (ready >= 0 && (!heartbeat || now - heartbeat >= 1000)) {
            if (write(ready, "R", 1) != 1) {
                return 1;
            }
            heartbeat = now;
        }
        fd_set readers;
        FD_ZERO(&readers);
        if (!health) {
            FD_SET(6, &readers);
        }
        struct timeval wait = {0, 100000};
        const int n = select(health ? 0 : 7, &readers, NULL, NULL, &wait);
        if (n < 0 && errno != EINTR) {
            return 1;
        }
        if (n > 0) {
            struct stat st;
            if (capture && (fstat(1, &st) || st.st_size > 16 * 1024 * 1024 - 65536)) {
                return 1; /* bounded private capture; retrieve/rename it before restarting */
            }
            uint8_t bytes[NBT_MOST_WIRE_RECORD_MAX + 1];
            const ssize_t count = recv(6, bytes, sizeof(bytes), 0);
            if (count <= 0 || !recorder.record(6, bytes, (size_t)count, now)) {
                return 1;
            }
        }
    }
    return 0;
}
