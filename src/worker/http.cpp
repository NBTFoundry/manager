// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/http.hpp"
#include "nbt/core/text.hpp"
#include <string.h>
namespace nbt {
namespace worker {
namespace http {
namespace {
const unsigned char *crlf(const unsigned char *p, const unsigned char *end) {
    while (p < end && static_cast<size_t>(end - p) >= 2) {
        if (p[0] == '\r' && p[1] == '\n') {
            return p;
        }
        ++p;
    }
    return NULL;
}
bool copy(char *out, size_t capacity, const unsigned char *value, size_t size) {
    if (!size || size >= capacity) {
        return false;
    }
    memcpy(out, value, size);
    out[size] = 0;
    return true;
}
} // namespace
void Request::clear() {
    method = Get;
    route = Nonce;
    host[0] = keyId[0] = nonce[0] = signature[0] = contentType[0] = 0;
    contentLength = 0;
    headerSize = 0;
    hasLength = false;
}
ParseResult parseRequest(core::Span<const uint8_t> data, Request *request) {
    if (!request || data.size() > kHeaderMax || (!data.data() && data.size())) {
        return Invalid;
    }
    request->clear();
    const unsigned char *bytes = data.data(), *end = NULL;
    for (size_t i = 0; i + 3 < data.size(); ++i) {
        if (!memcmp(bytes + i, "\r\n\r\n", 4)) {
            end = bytes + i + 4;
            break;
        }
    }
    if (!end) {
        return data.size() == kHeaderMax ? Invalid : Incomplete;
    }
    request->headerSize = static_cast<size_t>(end - bytes);
    const unsigned char *lineEnd = crlf(bytes, end), *cursor = NULL;
    if (!lineEnd) {
        return Invalid;
    }
    const size_t lineSize = static_cast<size_t>(lineEnd - bytes);
    if (lineSize >= 13 && !memcmp(bytes, "GET ", 4)) {
        cursor = bytes + 4;
    } else if (lineSize >= 14 && !memcmp(bytes, "POST ", 5)) {
        cursor = bytes + 5;
        request->method = Post;
    } else {
        return Invalid;
    }
    char route[40];
    if (lineEnd - cursor < 9 || memcmp(lineEnd - 9, " HTTP/1.1", 9) ||
        !copy(route, sizeof(route), cursor, static_cast<size_t>(lineEnd - 9 - cursor)) ||
        !routeForPath(route, request->route)) {
        return Invalid;
    }
    const unsigned char *line = lineEnd + 2;
    unsigned seen = 0, lines = 0;
    while (line < end - 2) {
        lineEnd = crlf(line, end);
        if (!lineEnd || lineEnd == line || ++lines > 32) {
            return Invalid;
        }
        const unsigned char *colon = static_cast<const unsigned char *>(
            memchr(line, ':', static_cast<size_t>(lineEnd - line)));
        if (!colon || colon == line || colon - line > 64) {
            return Invalid;
        }
        char name[65];
        for (cursor = line; cursor < colon; ++cursor) {
            unsigned char c = *cursor;
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<unsigned char>(c + ('a' - 'A'));
            }
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
                return Invalid;
            }
            name[cursor - line] = static_cast<char>(c);
        }
        name[colon - line] = 0;
        const unsigned char *value = colon + 1, *valueEnd = lineEnd;
        while (value < valueEnd && (*value == ' ' || *value == '\t')) {
            ++value;
        }
        while (valueEnd > value && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) {
            --valueEnd;
        }
        for (cursor = value; cursor < valueEnd; ++cursor) {
            if (*cursor < 32 || *cursor > 126) {
                return Invalid;
            }
        }
        const size_t size = static_cast<size_t>(valueEnd - value);
        unsigned bit = 0;
        if (!strcmp(name, "host")) {
            bit = 1;
            if (!copy(request->host, sizeof(request->host), value, size)) {
                return Invalid;
            }
        } else if (!strcmp(name, "content-length")) {
            char number[24];
            bit = 2;
            if (!copy(number, sizeof(number), value, size)) {
                return Invalid;
            }
            core::Result<uint64_t> length = core::parseU64(core::StringRef(number), false);
            if (!length.ok() || length.value() > kBodyMax) {
                return Invalid;
            }
            request->contentLength = length.value();
            request->hasLength = true;
        } else if (!strcmp(name, "content-type")) {
            bit = 4;
            if (!copy(request->contentType, sizeof(request->contentType), value, size)) {
                return Invalid;
            }
        } else if (!strcmp(name, "authorization") || !strcmp(name, "origin") ||
                   !strcmp(name, "transfer-encoding") || !strcmp(name, "content-encoding") ||
                   !strcmp(name, "expect") || !strcmp(name, "upgrade")) {
            return Invalid;
        } else if (!authValue(name, value, size, *request, bit)) {
            return Invalid;
        }
        if (seen & bit) {
            return Invalid;
        }
        seen |= bit;
        line = lineEnd + 2;
    }
    if (line != end - 2 || !(seen & 1)) {
        return Invalid;
    }
    const bool get = request->route == Nonce || request->route == Apps ||
                     request->route == Status || request->route == Manager ||
                     request->route == Keys || request->route == SelfTest;
    if ((request->method == Get) != get || (get && request->contentLength) ||
        (!get && !request->hasLength)) {
        return Invalid;
    }
    if (request->route == Nonce) {
        if (seen & 224) {
            return Invalid;
        }
    } else if ((seen & 224) != 224) {
        return Invalid;
    }
    if (streamed(request->route)) {
        if (!request->contentLength || strcmp(request->contentType, "application/octet-stream")) {
            return Invalid;
        }
    } else if (!get &&
               (request->contentLength > 2048 || strcmp(request->contentType, "text/plain"))) {
        return Invalid;
    }
    return Complete;
}
} // namespace http
} // namespace worker
} // namespace nbt
