/* HmiMain3D's environment as starter.cfg gives it: the Environment entries its
 * Process block requires (on the car: MALLOC_ARENA_SIZE, GRAPHICS_ROOT,
 * LD_LIBRARY_PATH, LD_PRELOAD). Read-only and strict: anything ambiguous
 * returns false and the caller tests without it. */
#include "starter_env.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace storage {
namespace {

const char kHmiProcess[] = "<Name>/opt/hmi/bin/zipped/HmiMain3D</Name>";

const char *find(const char *from, const char *end, const char *needle) {
    const size_t n = strlen(needle);
    for (const char *p = from; p && static_cast<size_t>(end - p) >= n; ++p) {
        if (*p == *needle && !memcmp(p, needle, n)) {
            return p;
        }
    }
    return NULL;
}

const char *findLast(const char *from, const char *end, const char *needle) {
    const char *last = NULL;
    for (const char *p = find(from, end, needle); p; p = find(p + 1, end, needle)) {
        last = p;
    }
    return last;
}

/* The text of the only <tag>...</tag> inside [from, end). */
bool onlyElement(const char *from, const char *end, const char *tag, const char **text,
                 size_t *len) {
    char open[32], close[32];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *start = find(from, end, open);
    if (!start || find(start + 1, end, open)) {
        return false;
    }
    start += strlen(open);
    const char *stop = find(start, end, close);
    if (!stop) {
        return false;
    }
    *text = start;
    *len = static_cast<size_t>(stop - start);
    return true;
}

bool validName(const char *s, size_t n) {
    if (!n || n > 31) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

bool validValue(const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < 0x21 || s[i] > 0x7e || s[i] == '<' || s[i] == '&') {
            return false;
        }
    }
    return true;
}

/* The Environment block whose <Number> is exactly `number`, if exactly one. */
bool environmentEntry(const char *data, const char *end, const char *number, HmiEnvEntry &out) {
    bool found = false;
    for (const char *open = find(data, end, "<Environment>"); open;
         open = find(open + 1, end, "<Environment>")) {
        const char *close = find(open, end, "</Environment>");
        const char *text = NULL;
        size_t len = 0;
        if (!close || !onlyElement(open, close, "Number", &text, &len)) {
            return false;
        }
        if (len != strlen(number) || memcmp(text, number, len)) {
            continue;
        }
        const char *name = NULL, *value = NULL;
        size_t nameLen = 0, valueLen = 0;
        if (found || !onlyElement(open, close, "EnvName", &name, &nameLen) ||
            !onlyElement(open, close, "Value", &value, &valueLen) || !validName(name, nameLen) ||
            !validValue(value, valueLen) || nameLen + 1 + valueLen >= sizeof(out.text)) {
            return false;
        }
        memcpy(out.text, name, nameLen);
        out.text[nameLen] = '=';
        memcpy(out.text + nameLen + 1, value, valueLen);
        out.text[nameLen + 1 + valueLen] = 0;
        out.nameLength = nameLen;
        found = true;
    }
    return found;
}

} // namespace

bool hmiStarterEnvironment(const char *data, size_t size, HmiEnv &env) {
    env.count = 0;
    const char *end = data + size;
    const char *name = find(data, end, kHmiProcess);
    if (!data || !name || find(name + 1, end, kHmiProcess)) {
        return false;
    }
    const char *open = findLast(data, name, "<Process>");
    const char *close = find(name, end, "</Process>");
    if (!open || !close) {
        return false;
    }
    static const char kRequires[] = "<RequiresEnvironment>";
    for (const char *p = find(open, close, kRequires); p; p = find(p + 1, close, kRequires)) {
        const char *digits = p + sizeof(kRequires) - 1;
        const char *stop = find(digits, close, "</RequiresEnvironment>");
        const size_t len = stop ? static_cast<size_t>(stop - digits) : 0;
        char number[4];
        if (!len || len >= sizeof(number) || env.count >= kHmiEnvMax ||
            strspn(digits, "0123456789") < len) {
            env.count = 0;
            return false;
        }
        memcpy(number, digits, len);
        number[len] = 0;
        if (!environmentEntry(data, end, number, env.entries[env.count])) {
            env.count = 0;
            return false;
        }
        ++env.count;
    }
    return env.count > 0;
}

} // namespace storage
} // namespace nbt
