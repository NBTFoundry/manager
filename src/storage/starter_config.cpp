// SPDX-License-Identifier: MPL-2.0
/* Byte-preserving parser and mutator for the capture entries of starter.cfg. */
#include "nbt/storage/starter_config.hpp"

#include "nbt/capture/protocol.h"

#include <string.h>

namespace nbt {
namespace storage {
namespace {

const char kStock[] = "libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so";
const char kInstalled[] =
    "libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so:" NBT_CAPTURE_SHIM_PATH;
// Environment 6 of a dev91-dev99 install.
const char kLegacy[] =
    "libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so:" NBT_CAPTURE_LEGACY_SHIM_PATH;
const char kEnvPath[] = NBT_CAPTURE_HMI_ENV_PATH;
const char kHmiName[] = "<Name>/opt/hmi/bin/zipped/HmiMain3D</Name>";
const char kEnvFileEmpty[] = "<EnvFile/>";
const char kEnvFileOn[] = "<EnvFile>" NBT_CAPTURE_HMI_ENV_PATH "</EnvFile>";

const char *findMem(const char *hay, size_t hayLen, const char *needle) {
    const size_t nlen = strlen(needle);
    if (nlen == 0 || hayLen < nlen) {
        return NULL;
    }
    for (size_t i = 0; i <= hayLen - nlen; ++i) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, nlen) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

int countExact(const char *hay, size_t len, const char *needle, const char **firstPos) {
    const size_t nlen = strlen(needle);
    int count = 0;
    const char *cur = hay;
    size_t rem = len;
    while (const char *hit = findMem(cur, rem, needle)) {
        if (count == 0 && firstPos) {
            *firstPos = hit;
        }
        ++count;
        const size_t step = static_cast<size_t>(hit - cur) + nlen;
        cur += step;
        rem -= step;
    }
    return count;
}

bool validEnv6Block(const char *blk, size_t len, StarterConfigSpan *span, const char *base) {
    const char *vStart = NULL;
    const char *vEnd = NULL;
    if (countExact(blk, len, "<Number>6</Number>", NULL) != 1 ||
        countExact(blk, len, "<Name>System_HMI_PRELOAD</Name>", NULL) != 1 ||
        countExact(blk, len, "<EnvName>LD_PRELOAD</EnvName>", NULL) != 1 ||
        countExact(blk, len, "<Number>", NULL) != 1 || countExact(blk, len, "<Name>", NULL) != 1 ||
        countExact(blk, len, "<EnvName>", NULL) != 1 ||
        countExact(blk, len, "<Value>", &vStart) != 1 ||
        countExact(blk, len, "</Value>", &vEnd) != 1) {
        return false;
    }
    vStart += 7; /* strlen("<Value>") */
    if (vEnd < vStart) {
        return false;
    }
    const size_t vLen = static_cast<size_t>(vEnd - vStart);
    StarterConfigState st = StarterInvalid;
    if (vLen == sizeof(kStock) - 1u && memcmp(vStart, kStock, vLen) == 0) {
        st = StarterStock;
    } else if (vLen == sizeof(kLegacy) - 1u && memcmp(vStart, kLegacy, vLen) == 0) {
        st = StarterLegacyShim;
    } else {
        return false;
    }
    if (span) {
        span->state = st;
        span->valueStart = static_cast<size_t>(vStart - base);
        span->valueEnd = static_cast<size_t>(vEnd - base);
    }
    return true;
}

/* HmiMain3D's Process block holds exactly one EnvFile element, empty or ours. */
bool findEnvFile(const char *data, size_t size, size_t *start, size_t *end, bool *on) {
    const char *name = NULL;
    if (countExact(data, size, kHmiName, &name) != 1) {
        return false;
    }
    const char *open = NULL;
    for (const char *cur = data;
         const char *hit = findMem(cur, static_cast<size_t>(name - cur), "<Process>");) {
        open = hit;
        cur = hit + 9;
    }
    const size_t after = static_cast<size_t>((data + size) - name);
    const char *close = findMem(name, after, "</Process>");
    if (!open || !close || findMem(name, static_cast<size_t>(close - name), "<Process>")) {
        return false;
    }
    const size_t blkLen = static_cast<size_t>(close - open);
    const char *elem = NULL;
    if (countExact(open, blkLen, "<EnvFile", &elem) != 1 ||
        countExact(open, blkLen, "</EnvFile", NULL) > 1) {
        return false;
    }
    const size_t room = static_cast<size_t>(close - elem);
    const size_t emptyLen = sizeof(kEnvFileEmpty) - 1u, onLen = sizeof(kEnvFileOn) - 1u;
    if (room >= emptyLen && !memcmp(elem, kEnvFileEmpty, emptyLen)) {
        *on = false;
        *end = static_cast<size_t>(elem - data) + emptyLen;
    } else if (room >= onLen && !memcmp(elem, kEnvFileOn, onLen)) {
        *on = true;
        *end = static_cast<size_t>(elem - data) + onLen;
    } else {
        return false;
    }
    *start = static_cast<size_t>(elem - data);
    return true;
}

/* The bytes outside the two spans, in file order: three pieces. */
void pieces(const StarterConfigSpan &sp, size_t size, size_t from[3], size_t to[3]) {
    const bool envFirst = sp.envFileStart < sp.valueStart;
    const size_t s1 = envFirst ? sp.envFileStart : sp.valueStart;
    const size_t e1 = envFirst ? sp.envFileEnd : sp.valueEnd;
    const size_t s2 = envFirst ? sp.valueStart : sp.envFileStart;
    const size_t e2 = envFirst ? sp.valueEnd : sp.envFileEnd;
    from[0] = 0, to[0] = s1;
    from[1] = e1, to[1] = s2;
    from[2] = e2, to[2] = size;
}

} // namespace

const char *StarterConfig::stockPreload() {
    return kStock;
}

const char *StarterConfig::installedPreload() {
    return kInstalled;
}

const char *StarterConfig::envFilePath() {
    return kEnvPath;
}

StarterConfigState StarterConfig::inspect(const char *data, size_t size, StarterConfigSpan *span) {
    if (span) {
        memset(span, 0, sizeof(*span));
        span->state = StarterInvalid;
    }
    if (!data || size == 0 || size > kStarterConfigMax || memchr(data, '\0', size) != NULL) {
        return StarterInvalid;
    }
    /* Reject duplicate System_HMI_PRELOAD anywhere in the file. */
    if (countExact(data, size, "<Name>System_HMI_PRELOAD</Name>", NULL) != 1) {
        return StarterInvalid;
    }
    int env6Blocks = 0;
    StarterConfigSpan found;
    memset(&found, 0, sizeof(found));
    found.state = StarterInvalid;
    const char *cur = data;
    size_t rem = size;
    while (const char *open = findMem(cur, rem, "<Environment>")) {
        const char *afterOpen = open + 13;
        const size_t afterLen = static_cast<size_t>((data + size) - afterOpen);
        const char *close = findMem(afterOpen, afterLen, "</Environment>");
        if (!close) {
            return StarterInvalid;
        }
        const char *nested =
            findMem(afterOpen, static_cast<size_t>(close - afterOpen), "<Environment>");
        if (nested) {
            return StarterInvalid;
        }
        const size_t blkLen = static_cast<size_t>(close - open);
        if (findMem(open, blkLen, "<Number>6</Number>") ||
            findMem(open, blkLen, "<Name>System_HMI_PRELOAD</Name>")) {
            if (++env6Blocks > 1 || !validEnv6Block(open, blkLen, &found, data)) {
                return StarterInvalid;
            }
        }
        cur = close + 14;
        rem = static_cast<size_t>((data + size) - cur);
    }
    bool envOn = false;
    if (env6Blocks != 1 ||
        !findEnvFile(data, size, &found.envFileStart, &found.envFileEnd, &envOn)) {
        return StarterInvalid;
    }
    if (envOn) {
        if (found.state != StarterStock) {
            return StarterInvalid; /* both forms at once */
        }
        found.state = StarterWithShim;
    }
    if (span) {
        *span = found;
    }
    return found.state;
}

bool StarterConfig::buildModified(const char *src, size_t srcSize, bool enable, char *dst,
                                  size_t dstCap, size_t *dstSize) {
    StarterConfigSpan span;
    if (inspect(src, srcSize, &span) == StarterInvalid || !dst || !dstSize) {
        return false;
    }
    const char *envFile = enable ? kEnvFileOn : kEnvFileEmpty;
    const bool envFirst = span.envFileStart < span.valueStart;
    const char *insert[2] = {envFirst ? envFile : kStock, envFirst ? kStock : envFile};
    size_t from[3], to[3], out = 0;
    pieces(span, srcSize, from, to);
    for (int i = 0; i < 3; ++i) {
        const size_t len = to[i] - from[i];
        if (len > dstCap - out) {
            return false;
        }
        memcpy(dst + out, src + from[i], len);
        out += len;
        if (i < 2) {
            const size_t insLen = strlen(insert[i]);
            if (insLen > dstCap - out) {
                return false;
            }
            memcpy(dst + out, insert[i], insLen);
            out += insLen;
        }
    }
    *dstSize = out;
    return true;
}

bool StarterConfig::matchesOriginOutsidePreload(const char *current, size_t currentSize,
                                                const char *orig, size_t origSize) {
    StarterConfigSpan curSpan, origSpan;
    if (!installed(inspect(current, currentSize, &curSpan)) ||
        inspect(orig, origSize, &origSpan) != StarterStock) {
        return false;
    }
    size_t cf[3], ct[3], of[3], ot[3];
    pieces(curSpan, currentSize, cf, ct);
    pieces(origSpan, origSize, of, ot);
    for (int i = 0; i < 3; ++i) {
        if (ct[i] - cf[i] != ot[i] - of[i] ||
            memcmp(current + cf[i], orig + of[i], ct[i] - cf[i]) != 0) {
            return false;
        }
    }
    return (curSpan.envFileStart < curSpan.valueStart) ==
           (origSpan.envFileStart < origSpan.valueStart);
}

} // namespace storage
} // namespace nbt
