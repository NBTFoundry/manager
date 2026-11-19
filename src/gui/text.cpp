#include "nbt/gui/text.hpp"

#include <string.h>

namespace nbt {
namespace gui {
namespace {

model::Language shown = model::LangEn;

/* The code point at `p` and its length in bytes; a malformed byte counts as
 * one character of width one. */
unsigned decode(const unsigned char *p, size_t &length) {
    unsigned need = 0, value = p[0];
    if (p[0] >= 0xf0 && p[0] < 0xf8) {
        need = 3;
        value = p[0] & 0x07u;
    } else if (p[0] >= 0xe0) {
        need = 2;
        value = p[0] & 0x0fu;
    } else if (p[0] >= 0xc0) {
        need = 1;
        value = p[0] & 0x1fu;
    }
    for (unsigned i = 1; i <= need; ++i) {
        if ((p[i] & 0xc0) != 0x80) {
            length = 1;
            return p[0];
        }
        value = (value << 6) | (p[i] & 0x3fu);
    }
    length = need + 1;
    return value;
}

bool wide(unsigned c) {
    return (c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf) ||
           (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff) ||
           (c >= 0xfe30 && c <= 0xfe4f) || (c >= 0xff00 && c <= 0xff60) ||
           (c >= 0xffe0 && c <= 0xffe6) || c >= 0x20000;
}

} // namespace

model::Language currentLanguage() {
    return shown;
}

void setCurrentLanguage(model::Language language) {
    if (model::languageCode(language)) {
        shown = language;
    }
}

const char *text(TextId id) {
    return textIn(shown, id);
}

void textf(char *out, size_t size, TextId id, const char *a, const char *b) {
    if (!out || !size) {
        return;
    }
    const char *p = text(id);
    size_t used = 0;
    bool cut = false;
    while (*p && !cut) {
        const char *insert = NULL;
        size_t n = 1;
        if (p[0] == '{' && (p[1] == '1' || p[1] == '2') && p[2] == '}') {
            insert = p[1] == '1' ? (a ? a : "") : (b ? b : "");
            p += 3;
            n = strlen(insert);
        } else {
            insert = p++;
        }
        if (used + n >= size) {
            n = size - 1 - used;
            cut = true;
        }
        memcpy(out + used, insert, n);
        used += n;
    }
    out[used] = 0;
    if (cut) {
        utf8TrimEnd(out);
    }
}

void utf8TrimEnd(char *s) {
    size_t start = strlen(s), continuation = 0;
    while (start && continuation < 4 && (static_cast<unsigned char>(s[start - 1]) & 0xc0) == 0x80) {
        --start;
        ++continuation;
    }
    if (!continuation) {
        const unsigned char last = start ? static_cast<unsigned char>(s[start - 1]) : 0;
        if (last >= 0xc0) { // a lead byte with nothing after it
            s[start - 1] = 0;
        }
        return;
    }
    if (!start) {
        s[0] = 0;
        return;
    }
    const unsigned char lead = static_cast<unsigned char>(s[start - 1]);
    const size_t need = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
    if (lead < 0xc0) {
        s[start] = 0; // continuation bytes without a lead byte
    } else if (continuation < need) {
        s[start - 1] = 0;
    }
}

size_t utf8Fit(const char *s, size_t max, size_t columns) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    size_t used = 0, width = 0;
    while (p[used]) {
        size_t length = 1;
        const unsigned c = decode(p + used, length);
        const size_t w = wide(c) ? 2 : 1;
        if (used + length > max || width + w > columns) {
            break;
        }
        used += length;
        width += w;
    }
    return used;
}

} // namespace gui
} // namespace nbt
