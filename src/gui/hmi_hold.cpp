// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/hmi_hold.hpp"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace gui {
namespace {

static const size_t kSignalMax = 48;
static const size_t kLivenessMax = 64;

// iDrive takes a focus event for our child state 14 only while its parent 15
// is on top (NBT-AAD fires its event 52 from its menu, never from a list; dev28
// fired ours from the Media list and iDrive ignored it). From anywhere else the
// parent comes first, as NBT-AAD's entry opens its own top-level menu.
unsigned focusAgain(HmiHold &hold, uint64_t now) {
    ++hold.tries;
    hold.focusMs = now;
    return hold.parent || hold.blank ? HoldFocus : HoldToParent;
}

unsigned finish(HmiHold &hold) {
    hold.phase = HoldIdle;
    return hold.blank || hold.parent ? HoldRelease : 0;
}

int openShmemChecked(const char *path, bool writing) {
    int flags = (writing ? (O_WRONLY | O_CREAT) : O_RDONLY) | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path, flags, 0600);
    if (fd < 0) {
        return -1;
    }
    struct stat st, named;
    bool valid = !fcntl(fd, F_SETFD, FD_CLOEXEC) && !fstat(fd, &st) && !lstat(path, &named);
    bool oneName = valid && st.st_nlink == 1;
#ifdef __QNXNTO__
    if (valid && !strncmp(path, "/dev/shmem/", 11)) {
        oneName = st.st_nlink >= 1 && st.st_nlink <= 3;
    }
#endif
    const mode_t forbidden = writing ? static_cast<mode_t>(0022) : static_cast<mode_t>(0002);
    valid = valid && S_ISREG(st.st_mode) && S_ISREG(named.st_mode) && st.st_uid == geteuid() &&
            oneName && !(st.st_mode & forbidden) && st.st_dev == named.st_dev &&
            st.st_ino == named.st_ino;
    if (!valid) {
        close(fd);
        errno = EPERM;
        return -1;
    }
    return fd;
}

// The new line goes over the old one in a single write, then the file is cut
// to its length: a reader polling meanwhile never finds it empty, which
// O_TRUNC would show it (NBT-AAD's state file, FIXES item 6). A longer old
// line leaves a tail until the cut; readers reject that line and read again.
int writeLine(const char *path, const char *text, size_t length) {
    const int fd = openShmemChecked(path, true);
    if (fd < 0) {
        return -1;
    }
    ssize_t count;
    do {
        count = write(fd, text, length);
    } while (count < 0 && errno == EINTR);
    int result = count == static_cast<ssize_t>(length) && !ftruncate(fd, static_cast<off_t>(length))
                     ? 0
                     : -1;
    if (close(fd)) {
        result = -1;
    }
    return result;
}

bool decimal(const char *text, size_t size, size_t &offset, uint64_t &value) {
    size_t digits = 0;
    value = 0;
    while (offset < size && text[offset] >= '0' && text[offset] <= '9') {
        const unsigned digit = static_cast<unsigned>(text[offset] - '0');
        if (digits >= 20 || value > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
        ++offset;
        ++digits;
    }
    if (!digits || (digits > 1 && text[offset - digits] == '0')) {
        return false;
    }
    return true;
}

} // namespace

HmiHold::HmiHold()
    : phase(HoldIdle), startedMs(0), focusMs(0), leftMs(0), parentMs(0), tries(0), blank(false),
      parent(false), seen(false), shown(false) {
}

void HmiHold::reset() {
    *this = HmiHold();
}

unsigned HmiHold::entry(uint64_t now, bool running) {
    unsigned actions = HoldPoll;
    if (phase == HoldStarting) {
    } else if (running) {
        phase = HoldOpen;
    } else {
        phase = HoldStarting;
        startedMs = now;
        actions |= HoldSpawn;
    }
    seen = blank;
    shown = false; // the GUI shows once iDrive is held on the blank state (poll)
    tries = 0;
    return actions | focusAgain(*this, now);
}

void HmiHold::focus(HoldState state, bool focused, uint64_t now) {
    if (state == HoldParent) {
        if (focused && !parent) {
            parentMs = now;
        }
        parent = focused;
        return;
    }
    if (focused && !blank) {
        seen = true;
        tries = 0;
    }
    if (!focused && blank) {
        leftMs = now;
    }
    blank = focused;
}

unsigned HmiHold::poll(uint64_t now, bool running) {
    switch (phase) {
    case HoldIdle:
        return 0;
    case HoldSteppedAside:
        if (running) {
            return HoldPoll;
        }
        phase = HoldIdle;
        return 0;
    case HoldStarting:
        if (running) {
            phase = HoldOpen;
        } else if (now - startedMs >= kHoldStartMs) {
            return finish(*this);
        }
        break;
    case HoldOpen:
        if (!running) {
            return finish(*this);
        }
        break;
    }
    if (blank) {
        // Knob, touch and the camera's focus change now reach this inert
        // state, not iDrive's menus: only from here may the GUI cover iDrive.
        if (phase == HoldOpen && !shown) {
            shown = true;
            return HoldShow | HoldPoll;
        }
        return HoldPoll;
    }
    if (seen && !parent) {
        if (phase == HoldOpen && now - leftMs >= 2u * kHoldPollMs) {
            phase = HoldSteppedAside;
            shown = false;
            return HoldAside | HoldPoll;
        }
        return HoldPoll;
    }
    // iDrive reached the parent since the last event: hold on the blank state now.
    const bool parentNew = parent && parentMs >= focusMs;
    if (!parentNew && (now - focusMs < (seen ? kHoldPollMs : 1000u) || tries >= kHoldTries)) {
        return HoldPoll;
    }
    return focusAgain(*this, now) | HoldPoll;
}

int holdSignalParse(const char *text, size_t size, uint64_t &sequence, HoldSignal &signal) {
    if (!text || !size || size > kSignalMax) {
        return -1;
    }
    size_t offset = 0;
    uint64_t value = 0;
    if (!decimal(text, size, offset, value) || offset >= size || text[offset++] != ' ') {
        return -1;
    }
    if (size - offset == 6 && !memcmp(text + offset, "aside\n", 6)) {
        signal = SignalAside;
    } else if (size - offset == 5 && !memcmp(text + offset, "show\n", 5)) {
        signal = SignalShow;
    } else {
        return -1;
    }
    sequence = value;
    return 0;
}

HoldReadResult holdSignalRead(const char *path, uint64_t &sequence, HoldSignal &signal) {
    char text[kSignalMax + 1];
    if (!path) {
        return HoldReadError;
    }
    const int fd = openShmemChecked(path, false);
    if (fd < 0) {
        return errno == ENOENT ? HoldReadAbsent : HoldReadError;
    }
    HoldReadResult result = HoldReadError;
    ssize_t count;
    do {
        count = read(fd, text, sizeof(text));
    } while (count < 0 && errno == EINTR);
    if (count > 0 && holdSignalParse(text, static_cast<size_t>(count), sequence, signal) == 0) {
        result = HoldReadOk;
    }
    close(fd);
    return result;
}

int holdSignalWrite(const char *path, uint64_t sequence, HoldSignal signal) {
    char text[kSignalMax];
    if (!path || (signal != SignalAside && signal != SignalShow)) {
        return -1;
    }
    const int length =
        snprintf(text, sizeof(text), "%llu %s\n", static_cast<unsigned long long>(sequence),
                 signal == SignalAside ? "aside" : "show");
    if (length < 0 || length >= static_cast<int>(sizeof(text))) {
        return -1;
    }
    return writeLine(path, text, static_cast<size_t>(length));
}

int holdLivenessWrite(const char *path, pid_t pid, uint64_t nowMs) {
    if (!path || pid <= 0) {
        return -1;
    }
    char text[kLivenessMax];
    const int length = snprintf(text, sizeof(text), "%ld %llu\n", static_cast<long>(pid),
                                static_cast<unsigned long long>(nowMs));
    if (length < 0 || length >= static_cast<int>(sizeof(text))) {
        return -1;
    }
    return writeLine(path, text, static_cast<size_t>(length));
}

bool holdLivenessRead(const char *path, uint64_t nowMs) {
    if (!path) {
        return false;
    }
    char text[kLivenessMax + 1];
    const int fd = openShmemChecked(path, false);
    if (fd < 0) {
        return false;
    }
    ssize_t count = -1;
    do {
        count = read(fd, text, sizeof(text));
    } while (count < 0 && errno == EINTR);
    close(fd);
    if (count <= 0 || static_cast<size_t>(count) > kLivenessMax) {
        return false;
    }
    size_t offset = 0;
    uint64_t pidValue = 0;
    uint64_t heartbeat = 0;
    if (!decimal(text, static_cast<size_t>(count), offset, pidValue) || !pidValue ||
        pidValue > INT_MAX || offset >= static_cast<size_t>(count) || text[offset++] != ' ' ||
        !decimal(text, static_cast<size_t>(count), offset, heartbeat) ||
        offset + 1 != static_cast<size_t>(count) || text[offset] != '\n' || nowMs < heartbeat ||
        nowMs - heartbeat > kHoldLivenessMs) {
        return false;
    }
    return kill(static_cast<pid_t>(pidValue), 0) == 0 || errno == EPERM;
}

} // namespace gui
} // namespace nbt
