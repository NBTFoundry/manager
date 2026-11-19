// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/phone_api.hpp"
#include "nbt/crypto/ec_public_key.hpp"
#include "nbt/util.h"
#include "nbt/worker/phone_network.hpp"
#include "nbt/worker/phone_routes.hpp"
#include "nbt/worker/worker.hpp"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
namespace nbt {
namespace worker {
PhoneApi::PhoneApi(Worker &worker)
    : worker_(worker), root_(worker.root()), phones_(root_), actions_(worker, phones_),
      files_(worker), door_(false), receiving_(false), systemLog_(false), wakeMs_(0), bssid_(),
      host_(), wakePath_() {
}
void PhoneApi::address(const char *host, const char *interfaceName) {
    snprintf(host_, sizeof(host_), "%s", host);
    phoneBssid(interfaceName, bssid_);
}
bool PhoneApi::initialize(bool door) {
    door_ = door;
    if (!phones_.initialize()) {
        return false;
    }
    if (!door && nbt_fs_unlink(root_.legacy(), NBT_FS_PHONE_UPLOAD, "manager", NULL) &&
        errno != ENOENT) {
        return false;
    }
    snprintf(wakePath_, sizeof(wakePath_), "/dev/shmem/nbt-manager.wake");
#ifdef NBT_TESTING
    if (!root_
             .path(NBT_FS_PHONE_WAKE, "manager", NULL,
                   core::Span<char>(wakePath_, sizeof(wakePath_)))
             .ok()) {
        return false;
    }
#endif
    return true;
}
bool PhoneApi::begin(const http::Request &request, uint64_t now) {
    actions_.expire(now);
    if (request.route == http::Nonce) {
        return true;
    }
    const bool files = request.route == http::FilesList || request.route == http::FilesPut ||
                       request.route == http::FilesData || request.route == http::FilesRemove;
    if (door_ && (request.route == http::Pair || request.route == http::Upload ||
                  request.route == http::Remove || request.route == http::Status || files)) {
        return false;
    }
    if (!nonces_.take(request.nonce, now) || !nonces_.signatureAllowed(now)) {
        return false;
    }
    if (request.route == http::Pair) {
        return actions_.canPair(now) && !receiving_ && worker_.state().state != State::Verified &&
               worker_.state().state != State::Installing;
    }
    storage::Phone phone;
    if (request.route == http::Status && actions_.candidate(request.keyId, phone)) {
        return true;
    }
    if (!phones_.find(request.keyId, phone)) {
        return false;
    }
    if (request.route == http::Upload || request.route == http::Remove) {
        return !receiving_ && !actions_.pending() && worker_.state().state != State::Verified &&
               !worker_.busy();
    }
    if (request.route == http::FilesData) {
        return files_.dataAllowed(request, now);
    }
    return true;
}
bool PhoneApi::authorized(const http::Request &request, const unsigned char hash[32],
                          const char *body) {
    storage::Phone phone;
    if (request.route == http::Pair) {
        if (!PhoneActions::parsePair(body, phone) || strcmp(phone.id, request.keyId)) {
            return false;
        }
    } else if (!(request.route == http::Status && actions_.candidate(request.keyId, phone)) &&
               !phones_.find(request.keyId, phone)) {
        return false;
    }
    unsigned char digest[32], signature[72];
    const size_t length = strlen(request.signature) / 2;
    return nbt_hex_decode(request.signature, signature, length) == 0 &&
           phoneRequestDigest(request.method == http::Get ? "GET" : "POST",
                              http::path(request.route), request.host, request.nonce,
                              request.contentLength, hash, digest) &&
           crypto::EcPublicKey::verify(phone.key, core::Span<const uint8_t>(digest, 32),
                                       core::Span<const uint8_t>(signature, length));
}
core::Result<core::UniqueFd> PhoneApi::uploadStart() {
    if (receiving_) {
        return core::Result<core::UniqueFd>::failure(core::Status::error(EBUSY, "upload busy"));
    }
    if (nbt_fs_unlink(root_.legacy(), NBT_FS_PHONE_UPLOAD, "manager", NULL) && errno != ENOENT) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("upload staging failed"));
    }
    core::Result<core::UniqueFd> fd =
        root_.open(NBT_FS_PHONE_UPLOAD, "manager", NULL, NBT_FS_CREATE);
    receiving_ = fd.ok();
    return fd;
}
void PhoneApi::uploadAbort() {
    receiving_ = false;
    (void)nbt_fs_unlink(root_.legacy(), NBT_FS_PHONE_UPLOAD, "manager", NULL);
}
int PhoneApi::finishUpload(const http::Request &request, uint64_t now, core::Span<char> out) {
    receiving_ = false;
    if (!worker_.openSession(NULL, now).ok() ||
        nbt_fs_rename(root_.legacy(), NBT_FS_PHONE_UPLOAD, NBT_FS_SESSION_UPLOAD, "manager",
                      NULL)) {
        uploadAbort();
        return 409;
    }
    worker_.state().session.setUploaded(true);
    worker_.pageOpened();
    char closed[32];
    (void)actions_.local("PHONE CLOSE", now, core::Span<char>(closed, sizeof(closed)));
    actions_.uploaded(request.keyId, now);
    if (!worker_.uploadEnd(now).ok() || worker_.state().state == State::Failed) {
        return 400;
    }
    // Verification runs on the worker's job (inline without a thread): until it
    // ends, "waiting"; /v1/status reports its outcome.
    const bool verified = worker_.state().state == State::Verified;
    snprintf(out.data(), out.size(), "STATUS\t%s\t%s\n", verified ? "verified" : "waiting",
             verified ? "Review and confirm on the car" : worker_.state().message);
    return 202;
}
int PhoneApi::logs(const http::Request &request, const char *body, uint64_t now, LogReply &reply) {
    if (request.route == http::LogsApp) {
        return phoneAppLog(root_, body, reply);
    }
    if (request.route == http::LogsManager) {
        return body[0] ? 400 : phoneManagerLog(root_, reply);
    }
    if (request.route != http::LogsSystem || body[0]) {
        return 400;
    }
    if (systemLog_) {
        return 409;
    }
    const int status = phoneSystemLogStart(now, reply);
    systemLog_ = status == 202;
    return status;
}
int PhoneApi::execute(const http::Request &request, const char *body, uint64_t now,
                      core::Span<char> out) {
    actions_.expire(now);
    switch (request.route) {
    case http::Nonce:
        snprintf(out.data(), out.size(), "NBT-REQ-1\n");
        return 200;
    case http::Apps:
        return phoneApplications(worker_, out);
    case http::SettingsRead:
    case http::SettingsSet:
        return phoneSettings(worker_, body, request.route == http::SettingsSet, out);
    case http::Pair:
        if (!bssid_[0]) {
            return 503;
        }
        return actions_.pair(request.keyId, request.nonce, body, now, out);
    case http::Remove:
        return actions_.remove(request.keyId, body, now, out);
    case http::Status:
        return actions_.status(request.keyId, now, bssid_, host_, out);
    case http::Wake:
        return door_ ? phoneWake(phones_, wakePath_, wakeMs_, now, out) : 404;
    case http::LogsClear: {
        const int status = phoneClearAppLog(root_, body);
        if (status == 200) {
            snprintf(out.data(), out.size(), "OK\n");
        }
        return status;
    }
    case http::FilesList:
        return files_.list(body, out);
    case http::FilesPut:
        return files_.put(request.keyId, body, now, out);
    case http::FilesRemove:
        return files_.remove(body, out);
    default:
        return 400;
    }
}
bool PhoneApi::local(const char *command, uint64_t now, core::Span<char> out) {
    return actions_.local(command, now, out);
}
} // namespace worker
} // namespace nbt
