// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/companion_files.hpp"

#include "nbt/companion_wire.h"
#include "nbt/storage/path_open.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

int companionPath(const char *dir, const char *app, const char *suffix, char *out, size_t size) {
    if (!dir || !app || !storage::SafePathOpen::validApp(app) || !strcmp(app, "manager")) {
        errno = EINVAL;
        return -1;
    }
    const int n = snprintf(out, size, "%s/nbt-bridge1-%s.%s", dir, app, suffix);
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : 0;
}

/* Our own regular file, opened read-only: the descriptor, -1 with ENOENT
 * when absent. */
int openOwn(const char *path) {
    struct stat named, st;
    if (lstat(path, &named)) {
        return -1;
    }
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = S_ISREG(named.st_mode) ? open(path, flags) : -1;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_dev != named.st_dev || st.st_ino != named.st_ino) {
        if (fd >= 0) {
            close(fd);
        }
        errno = EPERM;
        return -1;
    }
    return fd;
}

ssize_t readAll(int fd, char *out, size_t size) {
    ssize_t n;
    do {
        n = pread(fd, out, size, 0);
    } while (n < 0 && errno == EINTR);
    return n;
}

} // namespace

int companionEventsPath(const char *dir, const char *app, char *out, size_t size) {
    return companionPath(dir, app, "events", out, size);
}

int companionPageRead(const char *dir, const char *app, char *out, size_t size) {
    char path[160], again[NBT_COMPANION_PAGE_MAX + 1];
    if (!out || size < sizeof(again) || companionPath(dir, app, "page", path, sizeof(path))) {
        return -1;
    }
    const int fd = openOwn(path);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    // Rewritten in place: read until two reads agree (a page caught halfway
    // through its rewrite does not).
    ssize_t n = readAll(fd, out, sizeof(again));
    bool steady = false;
    for (unsigned tries = 0; n >= 0 && !steady && tries < 3; ++tries) {
        const ssize_t m = readAll(fd, again, sizeof(again));
        steady = m == n && !memcmp(out, again, static_cast<size_t>(n));
        if (!steady && m >= 0) {
            memcpy(out, again, static_cast<size_t>(m));
        }
        n = m;
    }
    close(fd);
    uint64_t sequence = 0;
    if (n < 0) {
        return -1;
    }
    return !steady || nbt_companion_page_check(out, static_cast<size_t>(n), &sequence)
               ? 0
               : static_cast<int>(n);
}

int companionEventAdd(const char *dir, const char *app, const char *id, const char *value) {
    char path[160], temporary[192], line[NBT_COMPANION_EVENT_LINE_MAX + 1];
    char old[NBT_COMPANION_EVENTS_MAX * (NBT_COMPANION_EVENT_LINE_MAX + 1)];
    char kept[sizeof(old) + sizeof(line)];
    if (companionPath(dir, app, "events", path, sizeof(path)) ||
        snprintf(temporary, sizeof(temporary), "%s.%ld", path, static_cast<long>(getpid())) >=
            static_cast<int>(sizeof(temporary))) {
        return -1;
    }
    ssize_t n = 0;
    const int fd = openOwn(path);
    if (fd >= 0) {
        n = readAll(fd, old, sizeof(old));
        close(fd);
    } else if (errno != ENOENT) {
        return -1;
    }
    // The valid lines, the last sequence among them.
    const char *lines[NBT_COMPANION_EVENTS_MAX];
    size_t lengths[NBT_COMPANION_EVENTS_MAX], count = 0;
    uint64_t last = 0;
    const size_t total = n > 0 ? static_cast<size_t>(n) : 0;
    for (size_t at = 0; at < total;) {
        const char *end = static_cast<const char *>(memchr(old + at, '\n', total - at));
        const size_t length = end ? static_cast<size_t>(end - (old + at)) + 1 : total - at;
        uint64_t sequence = 0;
        char seenId[NBT_COMPANION_ID_MAX + 1], seenValue[NBT_COMPANION_VALUE_MAX + 1];
        if (!nbt_companion_event_parse(old + at, length, &sequence, seenId, seenValue) &&
            sequence > last) {
            if (count == NBT_COMPANION_EVENTS_MAX) {
                memmove(lines, lines + 1, sizeof(lines[0]) * (count - 1));
                memmove(lengths, lengths + 1, sizeof(lengths[0]) * (count - 1));
                --count;
            }
            lines[count] = old + at;
            lengths[count++] = length;
            last = sequence;
        }
        at += length;
    }
    const int added = nbt_companion_event_format(last + 1, id, value, line, sizeof(line));
    if (added < 0) {
        errno = EINVAL;
        return -1;
    }
    size_t used = 0;
    for (size_t i = count == NBT_COMPANION_EVENTS_MAX ? 1 : 0; i < count; ++i) {
        memcpy(kept + used, lines[i], lengths[i]);
        used += lengths[i];
    }
    memcpy(kept + used, line, static_cast<size_t>(added));
    used += static_cast<size_t>(added);
    // Replaced whole: a reader sees the old list or the new one.
    (void)unlink(temporary);
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int out = open(temporary, flags, 0600);
    if (out < 0) {
        return -1;
    }
    ssize_t written;
    do {
        written = write(out, kept, used);
    } while (written < 0 && errno == EINTR);
    const bool closed = !close(out);
    if (written != static_cast<ssize_t>(used) || !closed || rename(temporary, path)) {
        const int saved = errno;
        (void)unlink(temporary);
        errno = saved ? saved : EIO;
        return -1;
    }
    return 0;
}

void companionRemove(const char *dir, const char *app) {
    char path[160];
    if (!companionPath(dir, app, "page", path, sizeof(path))) {
        (void)unlink(path);
    }
    if (!companionPath(dir, app, "events", path, sizeof(path))) {
        (void)unlink(path);
    }
}

} // namespace process
} // namespace nbt
