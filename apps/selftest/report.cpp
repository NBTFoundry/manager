// SPDX-License-Identifier: MPL-2.0
#include "report.hpp"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace selftest {
namespace {

const char *const kResults[] = {"pass", "fail", "skip"};

bool writeAll(int fd, const char *data, size_t size) {
    while (size) {
        const ssize_t n = write(fd, data, size);
        if (n <= 0) {
            return false;
        }
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

} // namespace

Report::Report(const char *form) : form_(form), lines_(), count_(0) {
}

void Report::set(const char *check, Result result, const char *format, ...) {
    size_t i = 0;
    while (i < count_ && strcmp(lines_[i].check, check)) {
        ++i;
    }
    if (i == count_) {
        if (count_ == kLines || strlen(check) >= sizeof(lines_[0].check)) {
            return;
        }
        ++count_;
    }
    Line &line = lines_[i];
    snprintf(line.check, sizeof(line.check), "%s", check);
    line.result = result;
    va_list args;
    va_start(args, format);
    vsnprintf(line.detail, sizeof(line.detail), format, args);
    va_end(args);
    for (char *c = line.detail; *c; ++c) {
        if (*c < 0x20 || *c > 0x7e) {
            *c = ' ';
        }
    }
}

bool Report::has(const char *check) const {
    for (size_t i = 0; i < count_; ++i) {
        if (!strcmp(lines_[i].check, check)) {
            return true;
        }
    }
    return false;
}

size_t Report::count(Result result) const {
    size_t n = 0;
    for (size_t i = 0; i < count_; ++i) {
        n += lines_[i].result == result ? 1 : 0;
    }
    return n;
}

bool Report::write(const char *dir) const {
    char path[512], temporary[520], text[2048];
    if (!dir || !dir[0] ||
        snprintf(path, sizeof(path), "%s/selftest.txt", dir) >= static_cast<int>(sizeof(path)) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
            static_cast<int>(sizeof(temporary))) {
        return false;
    }
    size_t used = static_cast<size_t>(snprintf(text, sizeof(text), "SELFTEST1\t%s\n", form_));
    for (size_t i = 0; i < count_ && used < sizeof(text); ++i) {
        const Line &line = lines_[i];
        used += static_cast<size_t>(snprintf(text + used, sizeof(text) - used, "%s\t%s\t%s\n",
                                             line.check, kResults[line.result], line.detail));
    }
    if (used >= sizeof(text)) {
        return false;
    }
    const int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return false;
    }
    const bool written = writeAll(fd, text, used) && fsync(fd) == 0;
    if (close(fd) || !written || rename(temporary, path)) {
        unlink(temporary);
        return false;
    }
    return true;
}

} // namespace selftest
