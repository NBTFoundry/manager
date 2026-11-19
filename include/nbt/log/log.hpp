// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LOG_LOG_HPP
#define NBT_LOG_LOG_HPP

#include "nbt/core/config.hpp"
#include "nbt/fs.h"
#include "nbt/log.h"

#include <stdarg.h>
#include <stdio.h>

namespace nbt {
namespace log {

enum OpenResult { kOpenError = -1, kOpenDisabled = 0, kOpenEnabled = 1 };

inline int open(nbt_fs *root, const char *component) {
    return nbt_log_open(root, component);
}
inline int openPath(const char *path, const char *component) {
    return nbt_log_open_path(path, component);
}
inline bool enabled() {
    return nbt_log_enabled() != 0;
}
inline void write(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char message[NBT_LOG_LINE + 1];
    if (vsnprintf(message, sizeof(message), format, arguments) >= 0) {
        nbt_logf("%s", message);
    }
    va_end(arguments);
}
inline void close() {
    nbt_log_close();
}

} // namespace log
} // namespace nbt

#endif
