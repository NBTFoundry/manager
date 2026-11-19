// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/usb_files.hpp"

#include "nbt/log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

const char *mountsFolder() {
#ifdef NBT_TESTING
    const char *path = getenv("NBT_TEST_USB_MOUNTS");
    if (path && path[0]) {
        return path;
    }
#endif
    return "/mnt";
}

bool allowedByte(unsigned char c) {
    return c >= 32 && c <= 126 && !strchr("/\\:*?\"<>|", c);
}

} // namespace

UsbFiles::UsbFiles()
    : app_(), phase_(Phase::Idle), files_(), matches_(), count_(0), selected_(kAll), reason_(),
      mutex_(), progress_(), buffer_() {
}

bool UsbFiles::storedName(const char *name, char out[NBT_FILES_NAME_MAX + 1]) {
    char clean[256];
    size_t n = 0;
    for (; name[n] && n < sizeof(clean) - 1; ++n) {
        const unsigned char c = static_cast<unsigned char>(name[n]);
        clean[n] = allowedByte(c) ? static_cast<char>(c) : '_';
    }
    clean[n] = 0;
    size_t start = 0;
    while (clean[start] == '.' || clean[start] == ' ') {
        ++start;
    }
    while (n > start && (clean[n - 1] == '.' || clean[n - 1] == ' ')) {
        clean[--n] = 0;
    }
    const char *dot = strrchr(clean + start, '.');
    const size_t length = n - start;
    if (!length || !dot) {
        return false;
    }
    if (length <= NBT_FILES_NAME_MAX) {
        memcpy(out, clean + start, length + 1);
    } else { // too long: the start of the name, then its extension
        const size_t extension = strlen(dot);
        if (extension >= NBT_FILES_NAME_MAX) {
            return false;
        }
        const size_t keep = NBT_FILES_NAME_MAX - extension;
        memcpy(out, clean + start, keep);
        memcpy(out + keep, dot, extension + 1);
    }
    return nbt_files_name_valid(out) != 0;
}

void UsbFiles::prepare(const char *app, const AppFiles &files) {
    if (strcmp(app_, app)) { // another application's stick listing does not apply
        count_ = 0;
        phase_ = Phase::Idle;
    }
    snprintf(app_, sizeof(app_), "%s", app);
    files_ = files;
    selected_ = kAll;
    reason_[0] = 0;
    core::LockGuard guard(mutex_);
    memset(&progress_, 0, sizeof(progress_));
}

bool UsbFiles::select(int index) {
    if (index != kAll && (index < 0 || static_cast<unsigned>(index) >= count_)) {
        return false;
    }
    selected_ = index;
    return true;
}

UsbProgress UsbFiles::progress() {
    core::LockGuard guard(mutex_);
    return progress_;
}

bool UsbFiles::stored(unsigned i) {
    core::LockGuard guard(mutex_);
    return i < count_ && matches_[i].stored;
}

void UsbFiles::advance(uint64_t bytes, bool file) {
    core::LockGuard guard(mutex_);
    progress_.doneBytes += bytes;
    progress_.doneFiles += file ? 1u : 0u;
}

void UsbFiles::walk(char *path, size_t length, unsigned depth, unsigned &entries) {
    DIR *dir = opendir(path);
    if (!dir) {
        return;
    }
    for (struct dirent *entry; count_ < kMatches && (entry = readdir(dir)) != NULL;) {
        const char *name = entry->d_name;
        const size_t n = strlen(name);
        struct stat st;
        if (name[0] == '.' || ++entries > kEntriesMax || length + 1 + n >= NBT_PATH_MAX) {
            if (entries > kEntriesMax) {
                break;
            }
            continue;
        }
        path[length] = '/';
        memcpy(path + length + 1, name, n + 1);
        if (!lstat(path, &st)) {
            UsbMatch &match = matches_[count_];
            if (S_ISDIR(st.st_mode) && depth < kDepthMax) {
                walk(path, length + 1 + n, depth + 1, entries);
            } else if (S_ISREG(st.st_mode) && length + 1 + n < sizeof(match.path) &&
                       storedName(name, match.name) &&
                       nbt_files_policy_accepts(&files_.policy, match.name)) {
                memcpy(match.path, path, length + 1 + n + 1);
                match.size = static_cast<uint64_t>(st.st_size);
                match.stored = false;
                ++count_;
            }
        }
        path[length] = 0;
    }
    closedir(dir);
}

int UsbFiles::scan() {
    char path[NBT_PATH_MAX];
    unsigned entries = 0;
    count_ = 0;
    const char *mounts = mountsFolder();
    DIR *dir = opendir(mounts);
    if (!dir) {
        snprintf(reason_, sizeof(reason_), "no-usb");
        return -1;
    }
    unsigned sticks = 0;
    for (struct dirent *entry; (entry = readdir(dir)) != NULL;) {
        struct stat st;
        const int n = snprintf(path, sizeof(path), "%s/%s", mounts, entry->d_name);
        // The stick's own folder may be a link to its mount; nothing under it is followed.
        if (strncmp(entry->d_name, "umass", 5) || n < 0 || static_cast<size_t>(n) >= sizeof(path) ||
            stat(path, &st) || !S_ISDIR(st.st_mode)) {
            continue;
        }
        ++sticks;
        walk(path, static_cast<size_t>(n), 1, entries);
    }
    closedir(dir);
    // In name order, so the car lists them as the phone does.
    for (unsigned i = 1; i < count_; ++i) {
        for (unsigned j = i; j > 0 && strcmp(matches_[j - 1].name, matches_[j].name) > 0; --j) {
            const UsbMatch swap = matches_[j];
            matches_[j] = matches_[j - 1];
            matches_[j - 1] = swap;
        }
    }
    nbt_logf("files %s: %u USB stick(s), %u file(s) found, %u entries read", app_, sticks, count_,
             entries);
    snprintf(reason_, sizeof(reason_), "%s", sticks ? "" : "no-usb");
    return sticks ? 0 : -1;
}

int UsbFiles::copy(UsbMatch &match) {
    struct stat st;
    int source = open(match.path, O_RDONLY
#ifdef O_NOFOLLOW
                                      | O_NOFOLLOW
#endif
    );
    if (source < 0 || fstat(source, &st) || !S_ISREG(st.st_mode) ||
        static_cast<uint64_t>(st.st_size) != match.size) {
        snprintf(reason_, sizeof(reason_), "changed");
        if (source >= 0) {
            close(source);
        }
        return -1;
    }
    core::Result<core::UniqueFd> staged = files_.root.stage(app_, storage::FilesSource::Usb);
    if (!staged.ok()) {
        close(source);
        snprintf(reason_, sizeof(reason_), "unavailable");
        return -1;
    }
    uint64_t left = match.size;
    while (left) {
        const size_t want = left < sizeof(buffer_) ? static_cast<size_t>(left) : sizeof(buffer_);
        const ssize_t got = read(source, buffer_, want);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        size_t written = 0;
        while (got > 0 && written < static_cast<size_t>(got)) {
            const ssize_t w =
                write(staged.value().get(), buffer_ + written, static_cast<size_t>(got) - written);
            if (w < 0 && errno == EINTR) {
                continue;
            }
            if (w <= 0) {
                break;
            }
            written += static_cast<size_t>(w);
        }
        if (got <= 0 || written != static_cast<size_t>(got)) {
            break;
        }
        left -= static_cast<uint64_t>(got);
        advance(static_cast<uint64_t>(got), false);
    }
    close(source);
    core::Status status = left || fsync(staged.value().get()) || !staged.value().close().ok()
                              ? core::Status::error(EIO, "copy failed")
                              : files_.root.commit(app_, storage::FilesSource::Usb, match.name);
    if (!status.ok()) {
        files_.root.discard(app_, storage::FilesSource::Usb);
        snprintf(reason_, sizeof(reason_), "%s", left ? "read" : filesReason(status));
        return -1;
    }
    return 0;
}

int UsbFiles::import() {
    {
        core::LockGuard guard(mutex_);
        memset(&progress_, 0, sizeof(progress_));
        for (unsigned i = 0; i < count_; ++i) {
            if ((selected_ == kAll || selected_ == static_cast<int>(i)) && !matches_[i].stored) {
                ++progress_.totalFiles;
                progress_.totalBytes += matches_[i].size;
            }
        }
    }
    int result = 0;
    for (unsigned i = 0; i < count_; ++i) {
        UsbMatch &match = matches_[i];
        if ((selected_ != kAll && selected_ != static_cast<int>(i)) || match.stored) {
            continue;
        }
        const core::Status admitted =
            files_.root.admit(app_, files_.policy, match.name, match.size);
        if (!admitted.ok() || copy(match)) {
            if (!admitted.ok()) {
                snprintf(reason_, sizeof(reason_), "%s", filesReason(admitted));
            }
            nbt_logf("files %s: %s not imported (%s)", app_, match.name, reason_);
            result = -1;
            // A full quota or disk ends the import; another file may still fit.
            if (!strcmp(reason_, "quota") || !strcmp(reason_, "space") ||
                !strcmp(reason_, "count") || !strcmp(reason_, "unavailable")) {
                break;
            }
            continue;
        }
        {
            core::LockGuard guard(mutex_);
            match.stored = true;
        }
        advance(0, true);
        nbt_logf("files %s: imported %s (%llu bytes)", app_, match.name,
                 static_cast<unsigned long long>(match.size));
    }
    return result;
}

} // namespace worker
} // namespace nbt
