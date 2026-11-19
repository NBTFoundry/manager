#ifndef NBT_CORE_SECURE_HPP
#define NBT_CORE_SECURE_HPP

#include <stddef.h>
#include <string.h>

namespace nbt {
namespace core {

inline void secureClear(void *buffer, size_t size) {
    volatile unsigned char *bytes = static_cast<volatile unsigned char *>(buffer);
    while (size--) {
        *bytes++ = 0;
    }
}

template <size_t N> class SecureBytes {
public:
    SecureBytes() {
        wipe();
    }
    SecureBytes(const SecureBytes &) = delete;
    SecureBytes &operator=(const SecureBytes &) = delete;
    ~SecureBytes() {
        wipe();
    }
    unsigned char *data() {
        return bytes_;
    }
    const unsigned char *data() const {
        return bytes_;
    }
    size_t size() const {
        return N;
    }
    void wipe() {
        secureClear(bytes_, sizeof(bytes_));
    }

private:
    unsigned char bytes_[N];
};

template <size_t N> class SecureString {
public:
    SecureString() : length_(0) {
        wipe();
    }
    SecureString(const SecureString &) = delete;
    SecureString &operator=(const SecureString &) = delete;
    ~SecureString() {
        wipe();
    }
    bool set(const char *text) {
        if (!text) {
            return false;
        }
        return set(text, strlen(text));
    }
    bool set(const char *text, size_t length) {
        if (!text || length > N) {
            return false;
        }
        wipe();
        memcpy(text_, text, length);
        text_[length] = 0;
        length_ = length;
        return true;
    }
    const char *data() const {
        return text_;
    }
    char *data() {
        return text_;
    }
    size_t size() const {
        return length_;
    }
    size_t capacity() const {
        return N;
    }
    void wipe() {
        secureClear(text_, sizeof(text_));
        length_ = 0;
    }

private:
    char text_[N + 1];
    size_t length_;
};

} // namespace core
} // namespace nbt

#endif
