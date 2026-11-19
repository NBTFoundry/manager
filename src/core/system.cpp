#include "nbt/core/system.hpp"
#include "nbt/core/checked.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#ifdef __QNX__
/* QNX 6.5's GCC 4.8 emits these hooks but supplies no libssp on this target.
 * The fixed guard is a platform limitation, not randomized stack protection. */
uintptr_t __stack_chk_guard = UINT32_C(0x9e3779b9);
extern "C" void __stack_chk_fail(void) {
    abort();
}
#endif

namespace nbt {
namespace core {

Result<uint64_t> monotonicMs() {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) || time.tv_nsec < 0 || time.tv_nsec >= 1000000000L) {
        return Result<uint64_t>::failure(Status::fromErrno("monotonic clock failed"));
    }
    const uint64_t seconds = static_cast<uint64_t>(time.tv_sec);
    uint64_t milliseconds;
    if (!checkedMul(seconds, UINT64_C(1000), &milliseconds) ||
        !checkedAdd(milliseconds, static_cast<uint64_t>(time.tv_nsec) / 1000000u, &milliseconds)) {
        return Result<uint64_t>::failure(Status::error(ERANGE, "monotonic clock overflow"));
    }
    return Result<uint64_t>::success(milliseconds);
}

Status randomBytes(Span<uint8_t> output) {
    if (!output.size() || output.size() > 256) {
        return Status::error(EINVAL, "invalid random byte count");
    }
    UniqueFd fd(open("/dev/urandom", O_RDONLY));
    if (!fd.valid()) {
        return Status::fromErrno("open random source failed");
    }
    size_t offset = 0;
    while (offset < output.size()) {
        const ssize_t count = read(fd.get(), output.data() + offset, output.size() - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return Status::fromErrno("read random source failed");
        }
        offset += static_cast<size_t>(count);
    }
    return fd.close();
}

bool constantEqual(Span<const uint8_t> left, Span<const uint8_t> right) {
    if (left.size() != right.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);
    }
    return difference == 0;
}

Status waitForFd(int fd, bool write, uint64_t deadlineMs) {
    if (fd < 0 || fd >= FD_SETSIZE) {
        return Status::error(EINVAL, "invalid descriptor");
    }
    for (;;) {
        Result<uint64_t> nowResult = monotonicMs();
        if (!nowResult.get()) {
            return nowResult.status();
        }
        const uint64_t now = nowResult.value();
        if (now >= deadlineMs) {
            return Status::error(ETIMEDOUT, "descriptor deadline exceeded");
        }
        const uint64_t remaining = deadlineMs - now;
        struct timeval timeout;
        timeout.tv_sec = static_cast<time_t>(remaining / 1000u);
        timeout.tv_usec = static_cast<suseconds_t>((remaining % 1000u) * 1000u);
        fd_set readSet, writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_SET(fd, write ? &writeSet : &readSet);
        const int result =
            select(fd + 1, write ? NULL : &readSet, write ? &writeSet : NULL, NULL, &timeout);
        if (result > 0) {
            return Status();
        }
        if (!result) {
            return Status::error(ETIMEDOUT, "descriptor deadline exceeded");
        }
        if (errno != EINTR) {
            return Status::fromErrno("select failed");
        }
    }
}

Status readWithDeadline(int fd, Span<uint8_t> output, uint64_t deadlineMs, size_t *readSize) {
    if (!readSize || output.size() > static_cast<size_t>(SSIZE_MAX)) {
        return Status::error(EINVAL, "invalid read buffer");
    }
    Status status = waitForFd(fd, false, deadlineMs);
    if (!status.ok()) {
        return status;
    }
    const ssize_t count = read(fd, output.data(), output.size());
    if (count < 0) {
        return Status::fromErrno("read failed");
    }
    *readSize = static_cast<size_t>(count);
    return Status();
}

Status writeWithDeadline(int fd, Span<const uint8_t> input, uint64_t deadlineMs) {
    if (input.size() > static_cast<size_t>(SSIZE_MAX)) {
        return Status::error(EINVAL, "invalid write buffer");
    }
    size_t offset = 0;
    while (offset < input.size()) {
        Status status = waitForFd(fd, true, deadlineMs);
        if (!status.ok()) {
            return status;
        }
        const ssize_t count = write(fd, input.data() + offset, input.size() - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return Status::fromErrno("write failed");
        }
        offset += static_cast<size_t>(count);
    }
    return Status();
}

Status childReapingInit() {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask)) {
        return Status::fromErrno("signal mask failed");
    }
    if (sigaction(SIGCHLD, &action, NULL)) {
        return Status::fromErrno("restore child reaping failed");
    }
    return Status();
}

} // namespace core
} // namespace nbt

int nbt_random(void *buffer, size_t size) {
    return nbt::core::randomBytes(nbt::core::Span<uint8_t>(static_cast<uint8_t *>(buffer), size))
                   .ok()
               ? 0
               : -1;
}

int nbt_constant_equal(const void *left, const void *right, size_t size) {
    return nbt::core::constantEqual(
               nbt::core::Span<const uint8_t>(static_cast<const uint8_t *>(left), size),
               nbt::core::Span<const uint8_t>(static_cast<const uint8_t *>(right), size))
               ? 1
               : 0;
}

int nbt_monotonic_ms(uint64_t *value) {
    if (!value) {
        return -1;
    }
    nbt::core::Result<uint64_t> result = nbt::core::monotonicMs();
    if (!result.get()) {
        return -1;
    }
    *value = result.value();
    return 0;
}

int nbt_child_reaping_init(void) {
    return nbt::core::childReapingInit().ok() ? 0 : -1;
}
