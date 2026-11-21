// SPDX-License-Identifier: MPL-2.0
/* A .zip archive of games from a USB stick (usb_files.hpp): listed from its
 * central directory, unpacked into a game folder or as single files. */
#include "nbt/worker/usb_files.hpp"

#include "nbt/log.h"
#include "nbt/storage/zip_archive.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
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

void UsbFiles::probeArchive(const char *path, size_t length) {
    UsbMatch &match = matches_[count_];
    const char *slash = lastSlash(path, length);
    char folder[NBT_FILES_FOLDER_MAX + 1], own[NBT_FILES_NAME_MAX + 1],
        stored[NBT_FILES_NAME_MAX + 1];
    storage::ZipArchive zip;
    const size_t named = slash ? length - static_cast<size_t>(slash + 1 - path) : 0;
    if (count_ >= kMatches || !slash || length >= sizeof(match.path) ||
        !storedFolderName(slash + 1, named - 4, folder) || !storedName(slash + 1, own) ||
        !zip.open(path)) {
        return;
    }
    unsigned files = 0;
    uint64_t bytes = 0;
    bool marked = false;
    for (unsigned i = 0; i < zip.count(); ++i) {
        if (accepted(storage::ZipArchive::baseName(zip.entry(i).name), stored)) {
            ++files;
            bytes += zip.entry(i).size;
            marked = marked || nbt_files_policy_marks_folder(&files_.policy, stored);
        }
    }
    if (!files) {
        return;
    }
    memcpy(match.path, path, length + 1);
    if (marked) {
        snprintf(match.name, sizeof(match.name), "%s/", folder);
    } else {
        memcpy(match.name, own, sizeof(match.name)); // the archive's own name
    }
    match.size = bytes;
    match.files = files;
    match.kind = marked ? UsbMatch::Archive : UsbMatch::ArchiveFiles;
    match.stored = false;
    ++count_;
}

int UsbFiles::importArchive(UsbMatch &match) {
    char staged[NBT_PATH_MAX], stored[NBT_FILES_NAME_MAX + 1];
    storage::ZipArchive zip;
    if (!zip.open(match.path)) {
        snprintf(reason_, sizeof(reason_), "changed");
        return -1;
    }
    if (!files_.root
             .stageFolder(app_, storage::FilesSource::Usb, core::Span<char>(staged, sizeof(staged)))
             .ok()) {
        snprintf(reason_, sizeof(reason_), "unavailable");
        return -1;
    }
    unsigned files = 0;
    for (unsigned i = 0; i < zip.count(); ++i) {
        const storage::ZipEntry &entry = zip.entry(i);
        if (!accepted(storage::ZipArchive::baseName(entry.name), stored)) {
            continue;
        }
        if (entry.size > nbt_files_policy_file_bytes(&files_.policy)) {
            snprintf(reason_, sizeof(reason_), "size");
            return -1;
        }
        core::Result<core::UniqueFd> out = files_.root.stageFolderFile(staged, stored);
        if (!out.ok()) {
            continue; // two entries made the same stored name: the first is kept
        }
        if (zip.extract(i, out.value().get(), &UsbFiles::advanced, this) ||
            fsync(out.value().get()) || !out.value().close().ok()) {
            nbt_logf("files %s: %s: %s cannot be unpacked", app_, match.name, entry.name);
            snprintf(reason_, sizeof(reason_), "read");
            return -1;
        }
        ++files;
    }
    if (!files) {
        snprintf(reason_, sizeof(reason_), "changed");
        return -1;
    }
    return 0;
}

int UsbFiles::importArchiveFiles(UsbMatch &match) {
    char stored[NBT_FILES_NAME_MAX + 1];
    storage::ZipArchive zip;
    if (!zip.open(match.path)) {
        snprintf(reason_, sizeof(reason_), "changed");
        return -1;
    }
    unsigned files = 0;
    for (unsigned i = 0; i < zip.count(); ++i) {
        const storage::ZipEntry &entry = zip.entry(i);
        if (!accepted(storage::ZipArchive::baseName(entry.name), stored)) {
            continue;
        }
        const core::Status admitted = files_.root.admit(app_, files_.policy, stored, entry.size);
        if (!admitted.ok()) {
            snprintf(reason_, sizeof(reason_), "%s", filesReason(admitted));
            return -1;
        }
        core::Result<core::UniqueFd> out = files_.root.stage(app_, storage::FilesSource::Usb);
        if (!out.ok()) {
            snprintf(reason_, sizeof(reason_), "unavailable");
            return -1;
        }
        const bool unpacked = !zip.extract(i, out.value().get(), &UsbFiles::advanced, this) &&
                              !fsync(out.value().get()) && out.value().close().ok();
        const core::Status status =
            unpacked ? files_.root.commit(app_, storage::FilesSource::Usb, stored)
                     : core::Status::error(EIO, "unpack failed");
        if (!status.ok()) {
            files_.root.discard(app_, storage::FilesSource::Usb);
            snprintf(reason_, sizeof(reason_), "%s", unpacked ? filesReason(status) : "read");
            return -1;
        }
        ++files;
    }
    if (!files) {
        snprintf(reason_, sizeof(reason_), "changed");
        return -1;
    }
    return 0;
}

} // namespace worker
} // namespace nbt
