// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CORE_CHECKED_HPP
#define NBT_CORE_CHECKED_HPP

#include <limits>
#include <stdint.h>
#include <type_traits>

namespace nbt {
namespace core {

template <typename T> bool checkedAdd(T left, T right, T *result) {
    if (!result || right > std::numeric_limits<T>::max() - left) {
        return false;
    }
    *result = static_cast<T>(left + right);
    return true;
}

template <typename T> bool checkedSub(T left, T right, T *result) {
    if (!result || left < right) {
        return false;
    }
    *result = static_cast<T>(left - right);
    return true;
}

template <typename T> bool checkedMul(T left, T right, T *result) {
    if (!result || (left && right > std::numeric_limits<T>::max() / left)) {
        return false;
    }
    *result = static_cast<T>(left * right);
    return true;
}

template <typename To, typename From> bool checkedNarrow(From value, To *result) {
    if (!result || (std::numeric_limits<From>::is_signed && value < 0)) {
        return false;
    }
    const uintmax_t wide = static_cast<uintmax_t>(value);
    if (wide > static_cast<uintmax_t>(std::numeric_limits<To>::max())) {
        return false;
    }
    *result = static_cast<To>(value);
    return true;
}

} // namespace core
} // namespace nbt

#endif
