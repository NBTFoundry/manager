// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/ipc_files.hpp"

#include "nbt/core/text.hpp"
#include "nbt/log.h"
#include "nbt/util.h"
#include "nbt/worker/web.hpp"
#include "nbt/worker/worker.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

char *word(char **cursor) {
    char *start = *cursor;
    if (!start || !*start || *start == ' ') {
        return NULL;
    }
    char *space = strchr(start, ' ');
    if (space) {
        *space = 0;
        *cursor = space + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

bool index(const char *text, unsigned limit, unsigned *out) {
    core::Result<uint64_t> value = core::parseU64(core::StringRef(text ? text : ""), false);
    if (!value.ok() || value.value() >= limit) {
        return false;
    }
    *out = static_cast<unsigned>(value.value());
    return true;
}

const char *phaseName(UsbFiles::Phase phase) {
    static const char *const kNames[] = {"idle",      "scanning", "scanned",
                                         "importing", "imported", "failed"};
    return kNames[static_cast<unsigned>(phase)];
}

/* Appends to a reply, false once it would not fit. */
bool append(size_t size, size_t *used, int n) {
    if (n < 0 || static_cast<size_t>(n) >= size - *used) {
        return false;
    }
    *used += static_cast<size_t>(n);
    return true;
}

} // namespace

IpcFiles::IpcFiles(storage::ManagedRoot &root, Worker &worker, WebServer &web,
                   ControlProtocol &control)
    : root_(root), worker_(worker), web_(web), control_(control), usb_(), pageApp_(), after_(),
      names_(), pageCount_(0) {
}

core::Status IpcFiles::refuse(const char *reason) {
    char reply[48];
    snprintf(reply, sizeof(reply), "ERR %s\n", reason);
    return control_.respond(reply);
}

core::Status IpcFiles::handle(char *cursor, uint64_t nowMs) {
    char *first = word(&cursor);
    char *app = first && (!strcmp(first, "DELETE") || !strcmp(first, "SCAN") ||
                          !strcmp(first, "USB") || !strcmp(first, "IMPORT"))
                    ? word(&cursor)
                    : first;
    char *argument = first != app || cursor ? word(&cursor) : NULL;
    if (!app || cursor) {
        return control_.respond("ERR invalid-command\n");
    }
    if (first == app) {
        if (!argument || (strcmp(argument, "first") && strcmp(argument, "next"))) {
            return control_.respond("ERR invalid-command\n");
        }
        return page(app, !strcmp(argument, "next"));
    }
    if (!strcmp(first, "DELETE")) {
        return remove(app, argument);
    }
    if (!strcmp(first, "USB")) {
        return usb(app, argument);
    }
    if (!strcmp(first, "SCAN")) {
        return argument ? control_.respond("ERR invalid-command\n")
                        : start(app, NULL, false, nowMs);
    }
    return start(app, argument, true, nowMs);
}

core::Status IpcFiles::page(const char *app, bool next) {
    AppFiles files;
    const int opened = openAppFiles(root_, app, files);
    if (opened) {
        return refuse(opened == 503 ? "unavailable" : "app");
    }
    if (!next || strcmp(pageApp_, app)) {
        after_[0] = 0;
    } else if (pageCount_) {
        memcpy(after_, names_[pageCount_ - 1], sizeof(after_));
    }
    storage::FileEntry entries[kPage + 1];
    size_t count = 0;
    core::Result<storage::FilesUsage> usage =
        files.root.list(app, after_, entries, kPage + 1, &count);
    core::Result<uint64_t> free = files.root.freeBytes();
    if (!usage.ok() || !free.ok()) {
        return refuse("unavailable");
    }
    snprintf(pageApp_, sizeof(pageApp_), "%s", app);
    pageCount_ = count > kPage ? kPage : static_cast<unsigned>(count);
    const uint64_t room = free.value() > storage::FilesRoot::kReserveBytes
                              ? free.value() - storage::FilesRoot::kReserveBytes
                              : 0;
    char reply[2000];
    size_t used = 0;
    bool fits = append(
        sizeof(reply), &used,
        snprintf(reply, sizeof(reply), "FILES\t%u\t%llu\t%llu\t%llu\t%d", usage.value().count,
                 static_cast<unsigned long long>(usage.value().bytes),
                 static_cast<unsigned long long>(nbt_files_policy_quota_bytes(&files.policy)),
                 static_cast<unsigned long long>(room), count > kPage ? 1 : 0));
    for (unsigned i = 0; fits && i < pageCount_; ++i) {
        memcpy(names_[i], entries[i].name, sizeof(names_[i]));
        fits = append(sizeof(reply), &used,
                      snprintf(reply + used, sizeof(reply) - used, "\t%s\t%llu", entries[i].name,
                               static_cast<unsigned long long>(entries[i].size)));
    }
    if (!fits ||
        !append(sizeof(reply), &used, snprintf(reply + used, sizeof(reply) - used, "\n"))) {
        return refuse("unavailable");
    }
    return control_.respond(reply);
}

core::Status IpcFiles::remove(const char *app, const char *text) {
    unsigned i = 0;
    AppFiles files;
    if (strcmp(pageApp_, app) || !index(text, pageCount_, &i)) {
        return control_.respond("ERR invalid-command\n");
    }
    if (worker_.busy() || worker_.taskPending() || web_.api().filesReceiving()) {
        return refuse("busy");
    }
    const int opened = openAppFiles(root_, app, files);
    if (opened) {
        return refuse(opened == 503 ? "unavailable" : "app");
    }
    const core::Status removed = files.root.remove(app, names_[i]);
    if (!removed.ok()) {
        return refuse(filesReason(removed));
    }
    nbt_logf("files %s: %s deleted on the car", app, names_[i]);
    return control_.respond("OK\n");
}

core::Status IpcFiles::start(const char *app, const char *which, bool importing, uint64_t nowMs) {
    AppFiles files;
    unsigned i = 0;
    if (importing &&
        (!which || strcmp(usb_.app(), app) || usb_.phase() == UsbFiles::Phase::Idle ||
         usb_.running() || (strcmp(which, "all") && !index(which, usb_.count(), &i)))) {
        return control_.respond("ERR invalid-command\n");
    }
    if (worker_.busy() || worker_.taskPending() || web_.api().filesReceiving()) {
        return refuse("busy");
    }
    const int opened = openAppFiles(root_, app, files);
    if (opened) {
        return refuse(opened == 503 ? "unavailable" : "app");
    }
    usb_.prepare(app, files);
    if ((importing && !usb_.select(strcmp(which, "all") ? static_cast<int>(i) : UsbFiles::kAll)) ||
        !worker_.startFiles(usb_, importing, nowMs)) {
        return refuse("busy");
    }
    return control_.respond("OK\n");
}

core::Status IpcFiles::usb(const char *app, const char *text) {
    unsigned offset = 0;
    if (!text || !index(text, UsbFiles::kMatches + 1, &offset)) {
        return control_.respond("ERR invalid-command\n");
    }
    (void)worker_.collectTask(0); // an ended task's phase, without waiting for the next poll
    const bool mine = !strcmp(usb_.app(), app);
    const UsbFiles::Phase phase = mine ? usb_.phase() : UsbFiles::Phase::Idle;
    const UsbProgress progress = mine ? usb_.progress() : UsbProgress();
    const unsigned count = mine && phase != UsbFiles::Phase::Scanning ? usb_.count() : 0;
    const unsigned percent =
        progress.totalBytes ? static_cast<unsigned>(progress.doneBytes * 100u / progress.totalBytes)
                            : 0;
    char reply[2000];
    size_t used = 0;
    bool fits = append(sizeof(reply), &used,
                       snprintf(reply, sizeof(reply), "USB\t%s\t%s\t%u\t%u\t%u\t%u",
                                phaseName(phase), mine && usb_.reason()[0] ? usb_.reason() : "-",
                                progress.doneFiles, progress.totalFiles, percent, count));
    for (unsigned i = offset; fits && i < count && i < offset + kPage; ++i) {
        const UsbMatch &match = usb_.match(i);
        fits =
            append(sizeof(reply), &used,
                   snprintf(reply + used, sizeof(reply) - used, "\t%s\t%llu\t%d", match.name,
                            static_cast<unsigned long long>(match.size), usb_.stored(i) ? 1 : 0));
    }
    if (!fits ||
        !append(sizeof(reply), &used, snprintf(reply + used, sizeof(reply) - used, "\n"))) {
        return refuse("unavailable");
    }
    return control_.respond(reply);
}

} // namespace worker
} // namespace nbt
