// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_PHONE_FILES_HPP
#define NBT_WORKER_PHONE_FILES_HPP

/* The paired phone's file routes (capability `files`, docs/files-bridge.md):
 * list, put (announce a file: name, size, SHA-256), data (its bytes, within
 * 60 s, from the same phone) and remove. No prompt on the car: the signed
 * policy bounds what a file can be, and files are data, never run. One
 * transfer at a time, none while the worker is busy (an install, a removal,
 * a USB import). Replies are `OK`, a listing, or `ERR <reason>`. */

#include "nbt/core/span.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/files_policy.h"
#include "nbt/package.h"
#include "nbt/worker/app_files.hpp"
#include "nbt/worker/http.hpp"

#include <stdint.h>

namespace nbt {
namespace worker {

class Worker;

class PhoneFiles {
public:
    /* Entries per listing page; a page fits the 4 KiB reply. */
    static const unsigned kPage = 20;
    /* Phone transfers carry at most a package's size; larger files come by USB. */
    static const uint64_t kPhoneMax = UINT64_C(64) * 1024 * 1024;
    static const uint64_t kTicketMs = 60000;

    explicit PhoneFiles(Worker &worker);
    int list(const char *body, core::Span<char> out);
    int put(const char *key, const char *body, uint64_t now, core::Span<char> out);
    int remove(const char *body, core::Span<char> out);
    /* The data route: allowed for the ticket's phone and size, then staged. */
    bool dataAllowed(const http::Request &request, uint64_t now) const;
    core::Result<core::UniqueFd> dataStart(const http::Request &request, uint64_t now);
    int dataFinish(const http::Request &request, const unsigned char hash[32],
                   core::Span<char> out);
    void dataAbort();
    bool receiving() const {
        return receiving_;
    }

private:
    struct Ticket {
        char key[65];
        char app[NBT_ID_MAX + 1];
        char name[NBT_FILES_NAME_MAX + 1];
        uint64_t size;
        unsigned char sha256[32];
        uint64_t deadline;
    };

    bool busy(const char *app) const;
    int refuse(int status, const char *reason, core::Span<char> out) const;

    Worker &worker_;
    AppFiles files_;
    Ticket ticket_;
    bool receiving_;
};

} // namespace worker
} // namespace nbt

#endif
