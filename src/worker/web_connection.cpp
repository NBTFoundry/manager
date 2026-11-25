// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/web.hpp"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
namespace nbt {
namespace worker {
namespace {
/* A streamed body ended early or was refused: its staged file goes. */
void abortStream(PhoneApi &api, http::Route route) {
    if (route == http::FilesData) {
        api.filesAbort();
    } else {
        api.uploadAbort();
    }
}
} // namespace
WebConnection::WebConnection()
    : phase_(Phase::Header), used_(0), received_(0), lastMs_(0), shortBody_(), hash_(),
      startedMs_(0), replySize_(0), replySent_(0), file_(0) {
    input_[0] = 0;
    reply_[0] = 0;
}
void WebConnection::clear() {
    fd_.reset();
    uploadFd_.reset();
    phase_ = Phase::Header;
    used_ = 0;
    request_.clear();
    received_ = lastMs_ = startedMs_ = 0;
    replySize_ = replySent_ = 0;
    shortBody_[0] = 0;
    log_.reset();
    file_ = 0;
}
void WebConnection::acceptClient(core::UniqueFd fd, uint64_t now) {
    clear();
    fd_ = std::move(fd);
    lastMs_ = startedMs_ = now;
    (void)SHA256_Init(&hash_);
}
void WebConnection::closeClient(PhoneApi &api) {
    if (log_.pending()) {
        api.systemLogDone();
    }
    if (uploadFd_.valid()) {
        uploadFd_.reset();
        abortStream(api, request_.route);
    }
    clear();
}
core::Status WebConnection::reply(PhoneApi &api, uint64_t now, int status, const char *bodyText,
                                  bool files) {
    char nonce[65], header[96] = {};
    if (api.nextNonce(now, nonce)) {
        snprintf(header, sizeof(header), "X-NBT-Nonce: %s\r\n", nonce);
    } else {
        status = 503;
        bodyText = "ERR nonce-unavailable\n";
        files = false;
    }
    if (!files) {
        log_.reset();
    }
    const unsigned long long length = strlen(bodyText) + log_.total();
    const int n =
        snprintf(reply_, sizeof(reply_),
                 "HTTP/1.1 %d Result\r\nContent-Type: text/plain\r\nContent-Length: %llu\r\n"
                 "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n%sConnection: "
                 "close\r\n\r\n%s",
                 status, length, header, bodyText);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(reply_)) {
        return core::Status::error(ERANGE, "web reply too large");
    }
    replySize_ = static_cast<size_t>(n);
    replySent_ = 0;
    phase_ = Phase::Reply;
    return core::Status();
}
core::Status WebConnection::body(PhoneApi &api, const unsigned char *data, size_t size,
                                 uint64_t now) {
    if (received_ > request_.contentLength || size > request_.contentLength - received_) {
        return reply(api, now, 400, "ERR body-length\n");
    }
    if (phase_ != Phase::Body || SHA256_Update(&hash_, data, size) != 1) {
        return core::Status::error(EIO, "body hash failed");
    }
    if (http::streamed(request_.route)) {
        if (!uploadFd_.valid()) {
            return core::Status::error(EINVAL, "upload unavailable");
        }
        size_t used = 0;
        while (used < size) {
            const ssize_t n = write(uploadFd_.get(), data + used, size - used);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                return core::Status::fromErrno("upload write failed");
            }
            used += static_cast<size_t>(n);
        }
    } else {
        if (received_ + size >= sizeof(shortBody_) || memchr(data, 0, size)) {
            return reply(api, now, 400, "ERR body-format\n");
        }
        memcpy(shortBody_ + static_cast<size_t>(received_), data, size);
        shortBody_[static_cast<size_t>(received_) + size] = 0;
    }
    received_ += size;
    if (received_ != request_.contentLength) {
        return core::Status();
    }
    unsigned char hash[32];
    if (SHA256_Final(hash, &hash_) != 1) {
        return core::Status::error(EIO, "body hash failed");
    }
    if (request_.route != http::Nonce && !api.authorized(request_, hash, shortBody_)) {
        if (uploadFd_.valid()) {
            uploadFd_.reset();
            abortStream(api, request_.route);
        }
        return reply(api, now, 403, "ERR authentication\n");
    }
    char output[4160] = {}; // room to read a phone page whole (PROTOCOL.md replies stay 4096)
    int status;
    if (http::streamed(request_.route)) {
        if (fsync(uploadFd_.get()) || !uploadFd_.close().ok()) {
            uploadFd_.reset();
            abortStream(api, request_.route);
            return reply(api, now, 500, "ERR upload\n");
        }
        const core::Span<char> out(output, sizeof(output));
        status = request_.route == http::Upload ? api.finishUpload(request_, now, out)
                                                : api.filesFinish(request_, hash, out);
    } else if (request_.route == http::LogsApp || request_.route == http::LogsSystem ||
               request_.route == http::LogsManager || request_.route == http::CrashesRead) {
        status = api.logs(request_, shortBody_, now, log_);
        if (status == 202) {
            phase_ = Phase::Wait;
            return core::Status();
        }
        return status == 200 ? reply(api, now, 200, "", true)
                             : reply(api, now, status, "ERR request\n");
    } else {
        status = api.execute(request_, shortBody_, now, core::Span<char>(output, sizeof(output)));
    }
    // A refusal that names its reason (the files routes) keeps it.
    return reply(api, now, status,
                 status < 300                  ? (output[0] ? output : "OK\n")
                 : !strncmp(output, "ERR ", 4) ? output
                                               : "ERR request\n");
}
core::Status WebConnection::readClient(const char *host, PhoneApi &api, uint64_t now) {
    if (now < startedMs_ || now - startedMs_ > (phase_ == Phase::Header ? 5000u : 600000u)) {
        return core::Status::error(ETIMEDOUT, "connection deadline");
    }
    const size_t offset = phase_ == Phase::Header ? used_ : 0;
    if (offset == sizeof(input_)) {
        return core::Status::error(EOVERFLOW, "header too large");
    }
    const ssize_t count = recv(fd_.get(), input_ + offset, sizeof(input_) - offset, 0);
    if (count <= 0) {
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return core::Status();
        }
        return core::Status::error(ECONNRESET, "connection closed");
    }
    lastMs_ = now;
    if (phase_ == Phase::Header) {
        used_ += static_cast<size_t>(count);
        const http::ParseResult parsed =
            http::parseRequest(core::Span<const uint8_t>(input_, used_), &request_);
        if (parsed == http::Incomplete) {
            return core::Status();
        }
        if (parsed == http::Invalid) {
            return reply(api, now, 400, "ERR invalid-request\n");
        }
        if (strcmp(request_.host, host) || !api.begin(request_, now)) {
            return reply(api, now, 403, "ERR authentication\n");
        }
        if (http::streamed(request_.route)) {
            core::Result<core::UniqueFd> fd =
                request_.route == http::Upload ? api.uploadStart() : api.filesStart(request_, now);
            if (!fd.ok()) {
                return reply(api, now, 409, "ERR upload-busy\n");
            }
            uploadFd_ = std::move(fd.value());
        }
        phase_ = Phase::Body;
        return body(api, input_ + request_.headerSize, used_ - request_.headerSize, now);
    }
    return body(api, input_, static_cast<size_t>(count), now);
}
core::Status WebConnection::poll(PhoneApi &api, uint64_t now) {
    const int status = phoneSystemLogPoll(now, log_);
    if (status == 202) {
        return core::Status();
    }
    api.systemLogDone();
    return status == 200 ? reply(api, now, 200, "", true)
                         : reply(api, now, status, "ERR system-log\n");
}
/* The next piece of the log files into reply_; false when one cannot be read. */
bool WebConnection::refill() {
    while (file_ < kLogFiles && !log_.sizes[file_]) {
        ++file_;
    }
    if (file_ == kLogFiles) {
        return true;
    }
    const size_t want = log_.sizes[file_] < sizeof(reply_) ? static_cast<size_t>(log_.sizes[file_])
                                                           : sizeof(reply_);
    ssize_t n;
    do {
        n = read(log_.files[file_].get(), reply_, want);
    } while (n < 0 && errno == EINTR);
    if (n <= 0) {
        return false;
    }
    log_.sizes[file_] -= static_cast<uint64_t>(n);
    replySize_ = static_cast<size_t>(n);
    replySent_ = 0;
    return true;
}
bool WebConnection::writeReply(uint64_t now, bool *completed) {
    *completed = false;
    if (replySent_ == replySize_ && !refill()) {
        return false;
    }
    if (replySent_ < replySize_) {
        const ssize_t count = send(fd_.get(), reply_ + replySent_, replySize_ - replySent_, 0);
        if (count <= 0) {
            return count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
        }
        replySent_ += static_cast<size_t>(count);
        lastMs_ = now;
    }
    *completed = replySent_ == replySize_ && !log_.total();
    return true;
}
} // namespace worker
} // namespace nbt
