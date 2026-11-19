#ifndef NBT_CORE_STATUS_HPP
#define NBT_CORE_STATUS_HPP

#include "nbt/core/check.hpp"
#include "nbt/core/config.hpp"

#include <cstddef>
#include <errno.h>
#include <new>
#include <type_traits>
#include <utility>

namespace nbt {
namespace core {

enum ErrorCode {
    kOk = 0,
    kPermissionDenied = EPERM,
    kNotFound = ENOENT,
    kIoError = EIO,
    kBadDescriptor = EBADF,
    kChildError = ECHILD,
    kAgain = EAGAIN,
    kAccessDenied = EACCES,
    kBusy = EBUSY,
    kExists = EEXIST,
    kInvalidArgument = EINVAL,
    kNoSpace = ENOSPC,
    kOutOfRange = ERANGE,
    kLoop = ELOOP,
    kProtocolError = EPROTO,
    kOverflow = EOVERFLOW,
    kNotSupported = ENOTSUP,
    kConnectionReset = ECONNRESET,
    kTimedOut = ETIMEDOUT
};

class Status {
public:
    Status() : code_(kOk), message_("") {
    }
    static Status error(ErrorCode code, const char *message) {
        return Status(code != kOk ? static_cast<int>(code) : EINVAL,
                      message ? message : "operation failed");
    }
    static Status error(ErrorCode code, char *message) = delete;
    static Status error(int code, const char *message) {
        return Status(code ? code : EINVAL, message ? message : "operation failed");
    }
    static Status error(int code, char *message) = delete;
    static Status fromErrno(const char *message) {
        return error(errno ? errno : EIO, message);
    }
    static Status fromErrno(char *message) = delete;
    bool ok() const {
        return code_ == kOk;
    }
    int code() const {
        return code_;
    }
    ErrorCode errorCode() const {
        return static_cast<ErrorCode>(code_);
    }
    const char *message() const {
        return message_;
    }

private:
    Status(int code, const char *message) : code_(code), message_(message) {
    }
    int code_;
    const char *message_;
};

template <typename T> class Result {
public:
    static Result success(const T &value) {
        Result result;
        result.store(value);
        return result;
    }
    static Result success(T &&value) {
        Result result;
        result.store(std::move(value));
        return result;
    }
    static Result failure(Status status) {
        NBT_CHECK(!status.ok());
        Result result;
        result.status_ = status;
        return result;
    }
    Result() : status_(), has_value_(false) {
    }
    Result(const Result &) = delete;
    Result &operator=(const Result &) = delete;
    Result(Result &&other) : status_(other.status_), has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_) T(std::move(*other.get()));
            other.clear();
        }
    }
    Result &operator=(Result &&other) {
        if (this == &other) {
            return *this;
        }
        clear();
        status_ = other.status_;
        has_value_ = other.has_value_;
        if (has_value_) {
            new (&storage_) T(std::move(*other.get()));
            other.clear();
        }
        return *this;
    }
    ~Result() {
        clear();
    }
    const Status &status() const {
        return status_;
    }
    bool ok() const {
        return has_value_;
    }
    T *get() {
        return has_value_ ? reinterpret_cast<T *>(&storage_) : NULL;
    }
    const T *get() const {
        return has_value_ ? reinterpret_cast<const T *>(&storage_) : NULL;
    }
    T &value() {
        NBT_CHECK(has_value_);
        return *get();
    }
    const T &value() const {
        NBT_CHECK(has_value_);
        return *get();
    }

private:
    template <typename U> void store(U &&value) {
        new (&storage_) T(std::forward<U>(value));
        has_value_ = true;
    }
    void clear() {
        if (has_value_) {
            get()->~T();
        }
        has_value_ = false;
    }
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
    Status status_;
    bool has_value_;
};

} // namespace core
} // namespace nbt

#endif
