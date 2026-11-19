#ifndef NBT_CORE_SYSTEM_HPP
#define NBT_CORE_SYSTEM_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"

#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace core {

Result<uint64_t> monotonicMs();
Status randomBytes(Span<uint8_t> output);
bool constantEqual(Span<const uint8_t> left, Span<const uint8_t> right);
Status waitForFd(int fd, bool write, uint64_t deadlineMs);
Status readWithDeadline(int fd, Span<uint8_t> output, uint64_t deadlineMs, size_t *read);
Status writeWithDeadline(int fd, Span<const uint8_t> input, uint64_t deadlineMs);
Status childReapingInit();

} // namespace core
} // namespace nbt

#endif
