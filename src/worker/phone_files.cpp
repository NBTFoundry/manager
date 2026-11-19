// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/phone_files.hpp"

#include "nbt/core/text.hpp"
#include "nbt/log.h"
#include "nbt/util.h"
#include "nbt/worker/worker.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

/* Splits "a\tb...\n" in place into exactly `count` fields. */
bool fields(char *text, char **out, unsigned count) {
    const size_t n = strlen(text);
    if (!n || text[n - 1] != '\n') {
        return false;
    }
    text[n - 1] = 0;
    for (unsigned i = 0; i < count; ++i) {
        out[i] = text;
        char *tab = strchr(text, '\t');
        if (i + 1 == count) {
            return tab == NULL;
        }
        if (!tab) {
            return false;
        }
        *tab = 0;
        text = tab + 1;
    }
    return false;
}

bool copyBody(const char *body, char *text, size_t size) {
    if (!body || strlen(body) >= size) {
        return false;
    }
    memcpy(text, body, strlen(body) + 1);
    return true;
}

} // namespace

PhoneFiles::PhoneFiles(Worker &worker) : worker_(worker), files_(), ticket_(), receiving_(false) {
}

bool PhoneFiles::busy(const char *app) const {
    const WorkerState &state = worker_.state();
    return worker_.busy() || worker_.taskPending() ||
           (state.updateLock.valid() && !strcmp(app, state.app));
}

int PhoneFiles::refuse(int status, const char *reason, core::Span<char> out) const {
    snprintf(out.data(), out.size(), "ERR %s\n", reason);
    return status;
}

int PhoneFiles::list(const char *body, core::Span<char> out) {
    char text[NBT_ID_MAX + NBT_FILES_NAME_MAX + 8], *part[2];
    if (!copyBody(body, text, sizeof(text)) || !fields(text, part, 2) ||
        (part[1][0] && !nbt_files_name_valid(part[1]))) {
        return refuse(400, "request", out);
    }
    const int opened = openAppFiles(worker_.root(), part[0], files_);
    if (opened) {
        return refuse(opened, opened == 503 ? "unavailable" : "app", out);
    }
    storage::FileEntry entries[kPage + 1];
    size_t count = 0;
    core::Result<storage::FilesUsage> usage =
        files_.root.list(part[0], part[1], entries, kPage + 1, &count);
    core::Result<uint64_t> free = files_.root.freeBytes();
    if (!usage.ok() || !free.ok()) {
        return refuse(503, "unavailable", out);
    }
    const uint64_t room = free.value() > storage::FilesRoot::kReserveBytes
                              ? free.value() - storage::FilesRoot::kReserveBytes
                              : 0;
    char extensions[NBT_FILES_EXTENSIONS * (NBT_FILES_EXTENSION_MAX + 1)] = {};
    for (unsigned i = 0; i < files_.policy.count; ++i) {
        strcat(extensions, i ? "," : "");
        strcat(extensions, files_.policy.extensions[i]);
    }
    int n = snprintf(out.data(), out.size(), "FILES\t%u\t%llu\t%llu\t%llu\t%llu\t%d\t%s\n",
                     usage.value().count, static_cast<unsigned long long>(usage.value().bytes),
                     static_cast<unsigned long long>(nbt_files_policy_quota_bytes(&files_.policy)),
                     static_cast<unsigned long long>(nbt_files_policy_file_bytes(&files_.policy)),
                     static_cast<unsigned long long>(room), count > kPage ? 1 : 0, extensions);
    size_t used = n > 0 ? static_cast<size_t>(n) : out.size();
    for (size_t i = 0; i < count && i < kPage && used < out.size(); ++i) {
        n = snprintf(out.data() + used, out.size() - used, "%s\t%llu\n", entries[i].name,
                     static_cast<unsigned long long>(entries[i].size));
        used += n > 0 ? static_cast<size_t>(n) : out.size();
    }
    return used < out.size() ? 200 : 500;
}

int PhoneFiles::put(const char *key, const char *body, uint64_t now, core::Span<char> out) {
    char text[NBT_ID_MAX + NBT_FILES_NAME_MAX + 96], *part[4];
    Ticket ticket;
    memset(&ticket, 0, sizeof(ticket));
    if (!copyBody(body, text, sizeof(text)) || !fields(text, part, 4) ||
        !nbt_files_name_valid(part[1]) || strlen(part[3]) != 64 ||
        nbt_hex_decode(part[3], ticket.sha256, sizeof(ticket.sha256))) {
        return refuse(400, "request", out);
    }
    core::Result<uint64_t> size = core::parseU64(core::StringRef(part[2]), false);
    if (!size.ok() || !size.value()) {
        return refuse(400, "request", out);
    }
    if (size.value() > kPhoneMax) {
        return refuse(413, "phone-size", out);
    }
    if (receiving_ || busy(part[0])) {
        return refuse(409, "busy", out);
    }
    const int opened = openAppFiles(worker_.root(), part[0], files_);
    if (opened) {
        return refuse(opened, opened == 503 ? "unavailable" : "app", out);
    }
    const core::Status admitted = files_.root.admit(part[0], files_.policy, part[1], size.value());
    if (!admitted.ok()) {
        return refuse(filesHttpStatus(admitted), filesReason(admitted), out);
    }
    memcpy(ticket.key, key, strlen(key) + 1);
    memcpy(ticket.app, part[0], strlen(part[0]) + 1);
    memcpy(ticket.name, part[1], strlen(part[1]) + 1);
    ticket.size = size.value();
    ticket.deadline = now + kTicketMs;
    ticket_ = ticket;
    snprintf(out.data(), out.size(), "OK\n");
    return 200;
}

bool PhoneFiles::dataAllowed(const http::Request &request, uint64_t now) const {
    return !receiving_ && ticket_.deadline && now < ticket_.deadline &&
           !strcmp(ticket_.key, request.keyId) && request.contentLength == ticket_.size &&
           !busy(ticket_.app);
}

core::Result<core::UniqueFd> PhoneFiles::dataStart(const http::Request &request, uint64_t now) {
    if (!dataAllowed(request, now) || openAppFiles(worker_.root(), ticket_.app, files_)) {
        return core::Result<core::UniqueFd>::failure(core::Status::error(EBUSY, "files busy"));
    }
    core::Result<core::UniqueFd> fd = files_.root.stage(ticket_.app, storage::FilesSource::Phone);
    receiving_ = fd.ok();
    return fd;
}

int PhoneFiles::dataFinish(const http::Request &request, const unsigned char hash[32],
                           core::Span<char> out) {
    receiving_ = false;
    Ticket ticket = ticket_;
    memset(&ticket_, 0, sizeof(ticket_));
    if (strcmp(ticket.key, request.keyId) || memcmp(ticket.sha256, hash, 32)) {
        files_.root.discard(ticket.app, storage::FilesSource::Phone);
        return refuse(400, "digest", out);
    }
    // Admitted again: the files may have changed since the ticket.
    core::Status status = files_.root.admit(ticket.app, files_.policy, ticket.name, ticket.size);
    if (status.ok()) {
        status = files_.root.commit(ticket.app, storage::FilesSource::Phone, ticket.name);
    }
    if (!status.ok()) {
        files_.root.discard(ticket.app, storage::FilesSource::Phone);
        return refuse(filesHttpStatus(status), filesReason(status), out);
    }
    nbt_logf("files %s: received %s (%llu bytes)", ticket.app, ticket.name,
             static_cast<unsigned long long>(ticket.size));
    snprintf(out.data(), out.size(), "OK\n");
    return 200;
}

void PhoneFiles::dataAbort() {
    if (receiving_) {
        files_.root.discard(ticket_.app, storage::FilesSource::Phone);
    }
    receiving_ = false;
    memset(&ticket_, 0, sizeof(ticket_));
}

int PhoneFiles::remove(const char *body, core::Span<char> out) {
    char text[NBT_ID_MAX + NBT_FILES_NAME_MAX + 8], *part[2];
    if (!copyBody(body, text, sizeof(text)) || !fields(text, part, 2) ||
        !nbt_files_name_valid(part[1])) {
        return refuse(400, "request", out);
    }
    if (receiving_ || busy(part[0])) {
        return refuse(409, "busy", out);
    }
    const int opened = openAppFiles(worker_.root(), part[0], files_);
    if (opened) {
        return refuse(opened, opened == 503 ? "unavailable" : "app", out);
    }
    const core::Status removed = files_.root.remove(part[0], part[1]);
    if (!removed.ok()) {
        return refuse(filesHttpStatus(removed), filesReason(removed), out);
    }
    nbt_logf("files %s: %s deleted from the phone", part[0], part[1]);
    snprintf(out.data(), out.size(), "OK\n");
    return 200;
}

} // namespace worker
} // namespace nbt
