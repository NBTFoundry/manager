#include "nbt/gui/car_language.hpp"

#include "nbt/gui/phone_wake.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace gui {
namespace {

const char kMagic[] = "NBTLANG1\nvalue=";

/* Decimal digits at `p` up to kCarLanguageMax, no leading zero; advances `p`. */
bool number(const char *&p, unsigned &value) {
    const char *start = p;
    value = 0;
    while (*p >= '0' && *p <= '9' && p - start < 3) {
        value = value * 10 + static_cast<unsigned>(*p++ - '0');
    }
    return p != start && (p - start == 1 || *start != '0') && value <= kCarLanguageMax &&
           !(*p >= '0' && *p <= '9');
}

} // namespace

bool carLanguageParse(const char *json, unsigned &value) {
    const char prefix[] = "{\"language\":";
    if (!json || strncmp(json, prefix, sizeof(prefix) - 1)) {
        return false;
    }
    const char *p = json + sizeof(prefix) - 1;
    unsigned parsed = 0;
    if (!number(p, parsed) || strcmp(p, "}")) {
        return false;
    }
    value = parsed;
    return true;
}

bool carLanguageWrite(const char *path, unsigned value) {
    if (value > kCarLanguageMax) {
        return false;
    }
    const int fd = privateRecordOpen(path, true);
    if (fd < 0) {
        return false;
    }
    char text[32];
    const int n = snprintf(text, sizeof(text), "%s%u\n", kMagic, value);
    ssize_t written;
    do {
        written = write(fd, text, static_cast<size_t>(n));
    } while (written < 0 && errno == EINTR);
    const bool ok = written == n && !ftruncate(fd, n);
    return close(fd) == 0 && ok;
}

bool carLanguageRead(const char *path, unsigned &value) {
    const int fd = privateRecordOpen(path, false);
    if (fd < 0) {
        return false;
    }
    char text[32];
    ssize_t n;
    do {
        n = read(fd, text, sizeof(text) - 1);
    } while (n < 0 && errno == EINTR);
    if (close(fd) || n <= 0 || n >= static_cast<ssize_t>(sizeof(text) - 1) ||
        memchr(text, 0, static_cast<size_t>(n))) {
        return false;
    }
    text[n] = 0;
    const char *p = text;
    unsigned parsed = 0;
    if (strncmp(p, kMagic, sizeof(kMagic) - 1)) {
        return false;
    }
    p += sizeof(kMagic) - 1;
    if (!number(p, parsed) || strcmp(p, "\n")) {
        return false;
    }
    value = parsed;
    return true;
}

} // namespace gui
} // namespace nbt
