// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CORE_SPAN_HPP
#define NBT_CORE_SPAN_HPP

#include "nbt/core/check.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace nbt {
namespace core {

template <typename T> class Span {
public:
    Span() : data_(NULL), size_(0) {
    }
    Span(T *data, size_t size) : data_(data), size_(size) {
        NBT_CHECK(data || !size);
    }
    template <size_t N> Span(T (&data)[N]) : data_(data), size_(N) {
    }
    T *data() const {
        return data_;
    }
    size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }
    T &operator[](size_t index) const {
        NBT_CHECK(index < size_);
        return data_[index];
    }
    bool get(size_t index, T *value) const {
        if (!value || index >= size_) {
            return false;
        }
        *value = data_[index];
        return true;
    }
    Span<T> slice(size_t offset, size_t size) const {
        NBT_CHECK(offset <= size_ && size <= size_ - offset);
        return Span<T>(data_ + offset, size);
    }

private:
    T *data_;
    size_t size_;
};

class StringRef {
public:
    StringRef() : data_(NULL), size_(0) {
    }
    StringRef(const char *data, size_t size) : data_(data), size_(size) {
        NBT_CHECK(data || !size);
    }
    StringRef(const char *text) : data_(text), size_(text ? strlen(text) : 0) {
    }
    const char *data() const {
        return data_;
    }
    size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }
    bool operator==(StringRef other) const {
        return size_ == other.size_ && (!size_ || memcmp(data_, other.data_, size_) == 0);
    }
    bool startsWith(StringRef prefix) const {
        return prefix.size_ <= size_ &&
               (!prefix.size_ || memcmp(data_, prefix.data_, prefix.size_) == 0);
    }
    StringRef slice(size_t offset, size_t size) const {
        NBT_CHECK(offset <= size_ && size <= size_ - offset);
        return StringRef(data_ + offset, size);
    }

private:
    const char *data_;
    size_t size_;
};

class ByteReader {
public:
    explicit ByteReader(Span<const uint8_t> bytes) : bytes_(bytes), offset_(0) {
    }
    size_t remaining() const {
        return bytes_.size() - offset_;
    }
    size_t position() const {
        return offset_;
    }
    bool skip(size_t size) {
        if (size > remaining()) {
            return false;
        }
        offset_ += size;
        return true;
    }
    bool readU8(uint8_t *value) {
        if (!value || remaining() < 1) {
            return false;
        }
        *value = bytes_[offset_++];
        return true;
    }
    bool readU16(uint16_t *value) {
        if (!value || remaining() < 2) {
            return false;
        }
        *value = static_cast<uint16_t>(bytes_[offset_]) |
                 static_cast<uint16_t>(static_cast<uint16_t>(bytes_[offset_ + 1]) << 8);
        offset_ += 2;
        return true;
    }
    bool readU32(uint32_t *value) {
        if (!value || remaining() < 4) {
            return false;
        }
        *value = static_cast<uint32_t>(bytes_[offset_]) |
                 (static_cast<uint32_t>(bytes_[offset_ + 1]) << 8) |
                 (static_cast<uint32_t>(bytes_[offset_ + 2]) << 16) |
                 (static_cast<uint32_t>(bytes_[offset_ + 3]) << 24);
        offset_ += 4;
        return true;
    }
    bool readU64(uint64_t *value) {
        uint32_t low, high;
        if (!value || !readU32(&low) || !readU32(&high)) {
            return false;
        }
        *value = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
        return true;
    }
    bool readBytes(Span<const uint8_t> *value, size_t size) {
        if (!value || size > remaining()) {
            return false;
        }
        *value = bytes_.slice(offset_, size);
        offset_ += size;
        return true;
    }

private:
    Span<const uint8_t> bytes_;
    size_t offset_;
};

class ByteWriter {
public:
    explicit ByteWriter(Span<uint8_t> bytes) : bytes_(bytes), offset_(0) {
    }
    size_t remaining() const {
        return bytes_.size() - offset_;
    }
    size_t position() const {
        return offset_;
    }
    bool writeU8(uint8_t value) {
        if (remaining() < 1) {
            return false;
        }
        bytes_[offset_++] = value;
        return true;
    }
    bool writeU16(uint16_t value) {
        if (remaining() < 2) {
            return false;
        }
        bytes_[offset_++] = static_cast<uint8_t>(value);
        bytes_[offset_++] = static_cast<uint8_t>(value >> 8);
        return true;
    }
    bool writeU32(uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            if (!writeU8(static_cast<uint8_t>(value >> (i * 8)))) {
                return false;
            }
        }
        return true;
    }
    bool writeU64(uint64_t value) {
        return writeU32(static_cast<uint32_t>(value)) &&
               writeU32(static_cast<uint32_t>(value >> 32));
    }
    bool writeBytes(Span<const uint8_t> value) {
        if (value.size() > remaining()) {
            return false;
        }
        if (value.size()) {
            memcpy(bytes_.data() + offset_, value.data(), value.size());
        }
        offset_ += value.size();
        return true;
    }

private:
    Span<uint8_t> bytes_;
    size_t offset_;
};

} // namespace core
} // namespace nbt

#endif
