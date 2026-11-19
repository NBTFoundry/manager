// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/phone_wake.hpp"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
namespace nbt {
namespace gui {
namespace {
bool idValid(const char *id) {
    if (!id || strlen(id) != 32) {
        return false;
    }
    for (unsigned i = 0; i < 32; ++i) {
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}
int openPrivate(const char *path, bool writing) {
    int flags = writing ? O_RDWR | O_CREAT : O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path, flags | O_NONBLOCK, 0600);
    if (fd < 0) {
        return -1;
    }
    struct stat st, named;
    bool valid = !fcntl(fd, F_SETFD, FD_CLOEXEC) && !fstat(fd, &st) && !lstat(path, &named);
    bool oneName = valid && st.st_nlink == 1;
#ifdef __QNXNTO__
    // Car inventory: /dev/shmem objects naturally report 2 or 3 links.
    // Persistent managed-root files still require exactly one name.
    if (valid && !strncmp(path, "/dev/shmem/", 11)) {
        oneName = st.st_nlink >= 1 && st.st_nlink <= 3;
    }
#endif
    valid = valid && S_ISREG(st.st_mode) && S_ISREG(named.st_mode) && st.st_uid == geteuid() &&
            oneName && !(st.st_mode & 0077) && st.st_dev == named.st_dev &&
            st.st_ino == named.st_ino;
    if (!valid) {
        close(fd);
        return -1;
    }
    return fd;
}
} // namespace

int privateRecordOpen(const char *path, bool writing) {
    return openPrivate(path, writing);
}

bool phoneDoorClaim(const char *path) {
    const int fd = openPrivate(path, true);
    if (fd < 0) {
        return false;
    }
    char value[2] = {};
    const ssize_t n = read(fd, value, sizeof(value));
    const bool valid = !n || (n == 1 && value[0] >= '0' && value[0] < '3');
    if (!n) {
        value[0] = '0';
    }
    ++value[0];
    const bool result =
        valid && lseek(fd, 0, SEEK_SET) == 0 && write(fd, value, 1) == 1 && !ftruncate(fd, 1);
    return close(fd) == 0 && result;
}
bool phoneDoorReset(const char *path) {
    const int fd = openPrivate(path, true);
    if (fd < 0) {
        return false;
    }
    const bool ok = write(fd, "0", 1) == 1 && !ftruncate(fd, 1);
    return close(fd) == 0 && ok;
}
const char *phoneWakeName(PhoneWakeState state) {
    const char *const names[] = {"opening", "open", "busy", "moving", "off"};
    const unsigned i = static_cast<unsigned>(state);
    return i < 5 ? names[i] : "busy";
}
bool phoneWakeRead(const char *path, PhoneWake &wake) {
    const int fd = openPrivate(path, false);
    if (fd < 0) {
        return false;
    }
    char text[128];
    ssize_t n;
    do {
        n = read(fd, text, sizeof(text) - 1);
    } while (n < 0 && errno == EINTR);
    const bool closed = close(fd) == 0;
    if (!closed || n <= 0 || n >= static_cast<ssize_t>(sizeof(text) - 1) ||
        memchr(text, 0, static_cast<size_t>(n))) {
        return false;
    }
    return phoneWakeParse(text, static_cast<size_t>(n), wake);
}
namespace {
bool consume(const char *&p, const char *literal) {
    const size_t n = strlen(literal);
    if (strncmp(p, literal, n)) {
        return false;
    }
    p += n;
    return true;
}
bool number(const char *&p, uint64_t &value) {
    const char *start = p;
    value = 0;
    while (*p >= '0' && *p <= '9') {
        const unsigned digit = static_cast<unsigned>(*p - '0');
        if (p - start >= 20 || value > (UINT64_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        ++p;
    }
    return p != start && (p - start == 1 || *start != '0');
}
} // namespace
bool phoneWakeParse(const char *input, size_t size, PhoneWake &wake) {
    if (!input || !size || size >= 128 || memchr(input, 0, size)) {
        return false;
    }
    char text[128];
    memcpy(text, input, size);
    text[size] = 0;
    const char *p = text;
    if (!consume(p, "NBTWAKE1\nid=") || strlen(p) < 32) {
        return false;
    }
    memcpy(wake.id, p, 32);
    wake.id[32] = 0;
    p += 32;
    if (!idValid(wake.id) || !consume(p, "\nwhen=") || !number(p, wake.when) ||
        !consume(p, "\nstate=") || *p < '0' || *p > '4') {
        return false;
    }
    wake.state = static_cast<PhoneWakeState>(*p++ - '0');
    return consume(p, "\n") && !*p;
}
bool phoneWakeWrite(const char *path, const PhoneWake &wake) {
    if (!idValid(wake.id) || static_cast<unsigned>(wake.state) > 4) {
        return false;
    }
    const int fd = openPrivate(path, true);
    if (fd < 0) {
        return false;
    }
    char text[128];
    const int n =
        snprintf(text, sizeof(text), "NBTWAKE1\nid=%s\nwhen=%llu\nstate=%u\n", wake.id,
                 static_cast<unsigned long long>(wake.when), static_cast<unsigned>(wake.state));
    ssize_t written;
    do {
        written = write(fd, text, static_cast<size_t>(n));
    } while (written < 0 && errno == EINTR);
    const bool ok = written == n && !ftruncate(fd, n);
    return close(fd) == 0 && ok;
}
PhoneWakeState phoneWakeRule(bool enabled, bool known, unsigned speed, uint64_t speedMs, bool alone,
                             bool safe, uint64_t contextMs, bool open, uint64_t now) {
    if (!enabled) {
        return WakeOff;
    }
    if (!known || speed || now < speedMs || now - speedMs > 3000) {
        return WakeMoving;
    }
    if (open) {
        return WakeOpen;
    }
    if (!alone || !safe || now < contextMs || now - contextMs > 3000) {
        return WakeBusy;
    }
    return WakeOpening;
}
bool phoneSpeed(const char *json, unsigned &speed) {
    if (!json || strlen(json) > 64) {
        return false;
    }
    const char *p = json;
    uint64_t value;
    if (!consume(p, "{\"speedActual\":") || !number(p, value) || value > 65535 ||
        !consume(p, "}") || *p) {
        return false;
    }
    speed = static_cast<unsigned>(value);
    return true;
}
bool phoneContextSafe(const char *json) {
    // Exact observed ID6 home widget. Other contexts require manual opening
    // until their display/camera behavior has been validated on this firmware.
    const char prefix[] = "{\"graphicalContext\":{\"visibleContext\":3,\"focusIndex\":";
    if (!json || strlen(json) > 2048 || strncmp(json, prefix, sizeof(prefix) - 1)) {
        return false;
    }
    const char *p = json + sizeof(prefix) - 1;
    size_t n = strspn(p, "0123456789");
    if (!n || n > 10 || strncmp(p + n, ",\"listIndex\":", 13)) {
        return false;
    }
    p += n + 13;
    if (*p == '-') {
        ++p;
    }
    n = strspn(p, "0123456789");
    const char widget[] = ",\"widgetType\":\"LT_Table_ID6L_Pad_Title\",";
    return n && n <= 10 && !strncmp(p + n, widget, sizeof(widget) - 1);
}

} // namespace gui
} // namespace nbt
