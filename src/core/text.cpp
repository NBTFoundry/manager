// SPDX-License-Identifier: MPL-2.0
#include "nbt/core/text.hpp"
#include "nbt/core/check.hpp"
#include "nbt/core/checked.hpp"
#include "nbt/core/secure.hpp"
#include "nbt/log.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace core {

void invariantFailed(const char *file, int line) {
    nbt_logf("BUG %s:%d", file, line);
    fprintf(stderr, "BUG %s:%d\n", file, line);
    abort();
}

bool tokenValid(StringRef text, size_t maximum, unsigned flags) {
    if (text.empty() || text.size() > maximum) {
        return false;
    }
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text.data()[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
              ((flags & kTokenDot) && c == '.') ||
              ((flags & kTokenUpper) && c >= 'A' && c <= 'Z'))) {
            return false;
        }
    }
    return true;
}

Result<uint64_t> parseU64(StringRef text, bool allowLeadingZero) {
    uint64_t value = 0;
    if (text.empty() || (!allowLeadingZero && text.size() > 1 && text.data()[0] == '0')) {
        return Result<uint64_t>::failure(Status::error(EINVAL, "invalid unsigned integer"));
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (text.data()[i] < '0' || text.data()[i] > '9') {
            return Result<uint64_t>::failure(Status::error(EINVAL, "invalid unsigned integer"));
        }
        const unsigned digit = static_cast<unsigned>(text.data()[i] - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return Result<uint64_t>::failure(Status::error(ERANGE, "unsigned integer overflow"));
        }
        value = value * 10 + digit;
    }
    return Result<uint64_t>::success(value);
}

Status hexDecode(StringRef text, Span<uint8_t> output) {
    size_t encodedSize;
    if (!checkedMul(output.size(), static_cast<size_t>(2), &encodedSize) ||
        text.size() != encodedSize || (output.size() && !output.data())) {
        return Status::error(EINVAL, "invalid hexadecimal size");
    }
    for (size_t i = 0; i < output.size(); ++i) {
        unsigned value = 0;
        for (unsigned j = 0; j < 2; ++j) {
            const unsigned char c = static_cast<unsigned char>(text.data()[i * 2 + j]);
            unsigned digit;
            if (c >= '0' && c <= '9') {
                digit = static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<unsigned>(c - 'a' + 10);
            } else {
                return Status::error(EINVAL, "invalid hexadecimal digit");
            }
            value = (value << 4) | digit;
        }
        output[i] = static_cast<uint8_t>(value);
    }
    return Status();
}

Status hexEncode(Span<const uint8_t> input, Span<char> output) {
    static const char digits[] = "0123456789abcdef";
    if (input.size() > (SIZE_MAX - 1) / 2 || output.size() < input.size() * 2 + 1) {
        return Status::error(ENOSPC, "hexadecimal output too small");
    }
    for (size_t i = 0; i < input.size(); ++i) {
        output[i * 2] = digits[input[i] >> 4];
        output[i * 2 + 1] = digits[input[i] & 15];
    }
    output[input.size() * 2] = 0;
    return Status();
}

Status copyTerminated(StringRef text, Span<char> output) {
    if (!text.data() || !output.data() || text.size() >= output.size() ||
        (text.size() && memchr(text.data(), 0, text.size()) != NULL)) {
        return Status::error(EINVAL, "invalid or unterminable string slice");
    }
    if (text.size()) {
        memcpy(output.data(), text.data(), text.size());
    }
    output.data()[text.size()] = 0;
    return Status();
}

bool FieldReader::next(StringRef name, StringRef *value) {
    if (!value || offset_ > input_.size() || name.size() + 1 > input_.size() - offset_) {
        return false;
    }
    StringRef line = input_.slice(offset_, input_.size() - offset_);
    if (!line.startsWith(name) || line.data()[name.size()] != '=') {
        return false;
    }
    size_t end = name.size() + 1;
    while (end < line.size() && line.data()[end] != '\n') {
        ++end;
    }
    if (end == line.size()) {
        return false;
    }
    *value = line.slice(name.size() + 1, end - name.size() - 1);
    offset_ += end + 1;
    return true;
}

} // namespace core
} // namespace nbt

int nbt_token_valid(const char *text, size_t maximum, unsigned flags) {
    return text && nbt::core::tokenValid(nbt::core::StringRef(text), maximum, flags) ? 1 : 0;
}

int nbt_parse_u64(const char *text, uint64_t *value, int allowLeadingZero) {
    if (!value) {
        return -1;
    }
    nbt::core::Result<uint64_t> result =
        nbt::core::parseU64(nbt::core::StringRef(text), allowLeadingZero != 0);
    if (!result.get()) {
        return -1;
    }
    *value = result.value();
    return 0;
}

int nbt_hex_decode(const char *text, unsigned char *output, size_t size) {
    return text && nbt::core::hexDecode(nbt::core::StringRef(text),
                                        nbt::core::Span<uint8_t>(output, size))
                       .ok()
               ? 0
               : -1;
}

int nbt_hex(const unsigned char *bytes, size_t size, char *output, size_t outputSize) {
    return output && nbt::core::hexEncode(nbt::core::Span<const uint8_t>(bytes, size),
                                          nbt::core::Span<char>(output, outputSize))
                         .ok()
               ? 0
               : -1;
}

char *nbt_field(char **cursor, const char *name) {
    if (!cursor || !*cursor || !name) {
        return NULL;
    }
    const size_t size = strlen(name);
    char *text = *cursor;
    if (strncmp(text, name, size) || text[size] != '=') {
        return NULL;
    }
    text += size + 1;
    char *end = strchr(text, '\n');
    if (!end) {
        return NULL;
    }
    *end = 0;
    *cursor = end + 1;
    return text;
}

void nbt_clear(void *buffer, size_t size) {
    nbt::core::secureClear(buffer, size);
}
