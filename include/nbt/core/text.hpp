#ifndef NBT_CORE_TEXT_HPP
#define NBT_CORE_TEXT_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace core {

enum TokenFlags { kTokenUpper = 1, kTokenDot = 2 };
bool tokenValid(StringRef text, size_t maximum, unsigned flags);
Result<uint64_t> parseU64(StringRef text, bool allowLeadingZero);
Status hexDecode(StringRef text, Span<uint8_t> output);
Status hexEncode(Span<const uint8_t> input, Span<char> output);
Status copyTerminated(StringRef text, Span<char> output);

class FieldReader {
public:
    explicit FieldReader(StringRef input) : input_(input), offset_(0) {
    }
    bool next(StringRef name, StringRef *value);
    size_t position() const {
        return offset_;
    }

private:
    StringRef input_;
    size_t offset_;
};

} // namespace core
} // namespace nbt

#endif
