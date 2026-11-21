// SPDX-License-Identifier: MPL-2.0
/* Games of several files from a USB stick: a folder of accepted files, or a
 * .zip archive holding them, each imported whole as one game folder
 * (usb_files.hpp, docs/files-bridge.md). */
#include "nbt/worker/usb_files.hpp"

#include "nbt/log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

/* The last '/' of path[0, length), or NULL. */
const char *lastSlash(const char *path, size_t length) {
    for (size_t i = length; i-- > 0;) {
        if (path[i] == '/') {
            return path + i;
        }
    }
    return NULL;
}

} // namespace

size_t UsbFiles::cleanName(const char *name, size_t length, char clean[256]) {
    size_t n = 0;
    for (size_t i = 0; i < length && name[i] && n < 255;) {
        const size_t k = nbt_files_char_length(name + i, length - i);
        if (!k) {
            clean[n++] = '_';
            ++i;
            continue;
        }
        if (n + k > 255) {
            break;
        }
        memcpy(clean + n, name + i, k);
        n += k;
        i += k;
    }
    clean[n] = 0;
    return n;
}

size_t UsbFiles::wholeChars(const char *text, size_t max) {
    size_t n = 0;
    while (text[n]) {
        const size_t k = nbt_files_char_length(text + n, 4);
        const size_t step = k ? k : 1;
        if (n + step > max) {
            break;
        }
        n += step;
    }
    return n;
}

bool UsbFiles::storedFolderName(const char *name, size_t length,
                                char out[NBT_FILES_FOLDER_MAX + 1]) {
    size_t start = 0;
    char clean[256];
    size_t n = cleanName(name, length, clean);
    while (clean[start] == '.' || clean[start] == ' ') {
        ++start;
    }
    if (n - start > NBT_FILES_FOLDER_MAX) {
        n = start + wholeChars(clean + start, NBT_FILES_FOLDER_MAX);
    }
    while (n > start && (clean[n - 1] == '.' || clean[n - 1] == ' ')) {
        --n;
    }
    if (n == start) {
        return false;
    }
    memcpy(out, clean + start, n - start);
    out[n - start] = 0;
    return nbt_files_folder_valid(out) != 0;
}

bool UsbFiles::marksFolder(const char *path) const {
    DIR *dir = files_.policy.folder_marks ? opendir(path) : NULL;
    if (!dir) {
        return false;
    }
    bool marked = false;
    unsigned seen = 0;
    char stored[NBT_FILES_NAME_MAX + 1];
    for (struct dirent *entry;
         !marked && ++seen <= kEntriesMax && (entry = readdir(dir)) != NULL;) {
        marked = entry->d_name[0] != '.' && storedName(entry->d_name, stored) &&
                 nbt_files_policy_marks_folder(&files_.policy, stored);
    }
    closedir(dir);
    return marked;
}

int UsbFiles::copyInto(const char *from, uint64_t size, int to) {
    struct stat st;
    int source = open(from, O_RDONLY
#ifdef O_NOFOLLOW
                                | O_NOFOLLOW
#endif
    );
    if (source < 0 || fstat(source, &st) || !S_ISREG(st.st_mode) ||
        static_cast<uint64_t>(st.st_size) != size) {
        if (source >= 0) {
            close(source);
        }
        return -1;
    }
    uint64_t left = size;
    while (left) {
        const size_t want = left < sizeof(buffer_) ? static_cast<size_t>(left) : sizeof(buffer_);
        const ssize_t got = read(source, buffer_, want);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        size_t written = 0;
        while (got > 0 && written < static_cast<size_t>(got)) {
            const ssize_t w = write(to, buffer_ + written, static_cast<size_t>(got) - written);
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
    return left ? -1 : 0;
}

bool UsbFiles::accepted(const char *name, char out[NBT_FILES_NAME_MAX + 1]) const {
    return storedName(name, out) && nbt_files_policy_accepts(&files_.policy, out);
}

void UsbFiles::addFolder(const char *path, size_t length, unsigned files, uint64_t bytes) {
    UsbMatch &match = matches_[count_];
    const char *slash = lastSlash(path, length);
    char folder[NBT_FILES_FOLDER_MAX + 1];
    if (count_ >= kMatches || !slash || length >= sizeof(match.path) ||
        !storedFolderName(slash + 1, length - static_cast<size_t>(slash + 1 - path), folder)) {
        return;
    }
    memcpy(match.path, path, length);
    match.path[length] = 0;
    snprintf(match.name, sizeof(match.name), "%s/", folder);
    match.size = bytes;
    match.files = files;
    match.kind = UsbMatch::Folder;
    match.stored = false;
    ++count_;
}

int UsbFiles::importSet(UsbMatch &match) {
    if (match.kind == UsbMatch::ArchiveFiles) {
        return importArchiveFiles(match);
    }
    char folder[NBT_FILES_FOLDER_MAX + 1];
    const size_t n = strlen(match.name);
    memcpy(folder, match.name, n - 1); // without its '/'
    folder[n - 1] = 0;
    const core::Status admitted =
        files_.root.admitFolder(app_, files_.policy, folder, match.files, match.size);
    if (!admitted.ok()) {
        snprintf(reason_, sizeof(reason_), "%s", filesReason(admitted));
        return -1;
    }
    if (match.size && match.files &&
        (match.kind == UsbMatch::Folder ? importFolder(match) : importArchive(match))) {
        files_.root.discardFolder(app_, storage::FilesSource::Usb);
        return -1;
    }
    const core::Status status = files_.root.commitFolder(app_, storage::FilesSource::Usb, folder);
    if (!status.ok()) {
        files_.root.discardFolder(app_, storage::FilesSource::Usb);
        snprintf(reason_, sizeof(reason_), "%s", filesReason(status));
        return -1;
    }
    return 0;
}

int UsbFiles::importFolder(UsbMatch &match) {
    char staged[NBT_PATH_MAX], path[NBT_PATH_MAX], stored[NBT_FILES_NAME_MAX + 1];
    if (!files_.root
             .stageFolder(app_, storage::FilesSource::Usb, core::Span<char>(staged, sizeof(staged)))
             .ok()) {
        snprintf(reason_, sizeof(reason_), "unavailable");
        return -1;
    }
    DIR *dir = opendir(match.path);
    if (!dir) {
        snprintf(reason_, sizeof(reason_), "changed");
        return -1;
    }
    int result = 0;
    unsigned files = 0;
    for (struct dirent *entry; !result && (entry = readdir(dir)) != NULL;) {
        struct stat st;
        const int n = snprintf(path, sizeof(path), "%s/%s", match.path, entry->d_name);
        if (entry->d_name[0] == '.' || n < 0 || static_cast<size_t>(n) >= sizeof(path) ||
            lstat(path, &st) || !S_ISREG(st.st_mode) || !accepted(entry->d_name, stored)) {
            continue;
        }
        const uint64_t size = static_cast<uint64_t>(st.st_size);
        core::Result<core::UniqueFd> out = files_.root.stageFolderFile(staged, stored);
        if (size > nbt_files_policy_file_bytes(&files_.policy)) {
            snprintf(reason_, sizeof(reason_), "size");
            result = -1;
        } else if (!out.ok()) {
            continue; // two names made the same stored name: the first is kept
        } else if (copyInto(path, size, out.value().get()) || fsync(out.value().get()) ||
                   !out.value().close().ok()) {
            snprintf(reason_, sizeof(reason_), "read");
            result = -1;
        } else {
            ++files;
        }
    }
    closedir(dir);
    if (!result && !files) {
        snprintf(reason_, sizeof(reason_), "changed");
        result = -1;
    }
    return result;
}

} // namespace worker
} // namespace nbt
