#ifndef NBT_PROCESS_HMI_ENVIRONMENT_HPP
#define NBT_PROCESS_HMI_ENVIRONMENT_HPP

#include <stddef.h>
#include <string.h>

extern "C" char **environ;

namespace nbt {
namespace process {

static const size_t kHmiEnvironmentMax = 96;
// The capture shim, where it is now and where dev91-dev103 kept it.
static const char kCapturePreloadPath[] = "/var/opt/nbt-manager/state/manager/libnbt-capture.so";
static const char kLegacyCapturePreloadPath[] = "/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so";

inline bool isCapturePreload(const char *p, size_t len) {
    return (len == sizeof(kCapturePreloadPath) - 1 && !memcmp(p, kCapturePreloadPath, len)) ||
           (len == sizeof(kLegacyCapturePreloadPath) - 1 &&
            !memcmp(p, kLegacyCapturePreloadPath, len));
}

/* NBT-AAD's fixed PATH for the daemon it starts from the HMI. */
inline char *hmiPath() {
    static char path[] = "PATH=/bin:/usr/bin:/opt/os/bin:/opt/sys/bin:/opt/core/bin:/opt/car/bin:/"
                         "fs/sda0/opt/mm/bin";
    return path;
}

/* Removes only the exact capture shim entry from `entry` ("LD_PRELOAD=...") into
 * caller-owned `out[size]`, preserving all other entries and their exact order.
 * Sets `*omitted = true` when no entries remain; `*changed = true` when our entry
 * was removed. Returns false on overflow without leaving partial output used. */
inline bool stripCapturePreload(const char *entry, char *out, size_t size, bool *omitted,
                                bool *changed) {
    static const char kPrefix[] = "LD_PRELOAD=";
    const size_t prefixLen = sizeof(kPrefix) - 1;
    if (!entry || strncmp(entry, kPrefix, prefixLen)) {
        return false;
    }
    *omitted = false;
    *changed = false;
    const char *p = entry + prefixLen;
    if (!*p) {
        *omitted = true;
        return true;
    }
    if (size <= prefixLen) {
        return false;
    }
    memcpy(out, kPrefix, prefixLen);
    size_t used = prefixLen;
    bool wroteAny = false;
    while (*p) {
        const size_t rem = strlen(p);
        const char *colon = static_cast<const char *>(memchr(p, ':', rem));
        const size_t len = colon ? static_cast<size_t>(colon - p) : rem;
        if (isCapturePreload(p, len)) {
            *changed = true;
        } else if (len > 0) {
            const size_t need = (wroteAny ? 1u : 0u) + len + 1u;
            if (used + need > size) {
                return false;
            }
            if (wroteAny) {
                out[used++] = ':';
            }
            memcpy(out + used, p, len);
            used += len;
            wroteAny = true;
        } else if (!*changed) {
            /* Only preserve adjacent colons if we are not altering an untouched value. */
        }
        p += len;
        if (*p == ':') {
            ++p;
        }
    }
    if (!wroteAny) {
        *omitted = true;
        return true;
    }
    out[used] = '\0';
    return true;
}

/* The environment NBT-AAD gives the daemon it starts from the HMI (rhmiapp,
 * launch): its fixed PATH, then the source environment without its PATH,
 * stripping only our capture shim from LD_PRELOAD (or omitting LD_PRELOAD when
 * `includePreload` is false). Returns false on overflow (caller must abort).
 * No allocation, so the plugin can use it. */
inline bool hmiEnvironmentFrom(char *const *source, char *out[kHmiEnvironmentMax],
                               char *preloadOut = NULL, size_t preloadSize = 0,
                               bool includePreload = true) {
    size_t count = 0;
    bool seenPreload = false;
    out[count++] = hmiPath();
    for (char *const *entry = source; entry && *entry; ++entry) {
        if (!strncmp(*entry, "PATH=", 5)) {
            continue;
        }
        if (!strncmp(*entry, "LD_PRELOAD=", 11)) {
            if (!includePreload) {
                continue;
            }
            char *chosen = *entry;
            if (!seenPreload && preloadOut && preloadSize > 0) {
                bool omitted = false, changed = false;
                if (!stripCapturePreload(*entry, preloadOut, preloadSize, &omitted, &changed)) {
                    return false;
                }
                if (omitted) {
                    continue;
                }
                if (changed) {
                    chosen = preloadOut;
                    seenPreload = true;
                }
            } else if (strstr(*entry + 11, kCapturePreloadPath) ||
                       strstr(*entry + 11, kLegacyCapturePreloadPath)) {
                /* Cannot safely strip without caller storage or a duplicate LD_PRELOAD. */
                return false;
            }
            if (count >= kHmiEnvironmentMax - 1) {
                return false;
            }
            out[count++] = chosen;
            continue;
        }
        if (count >= kHmiEnvironmentMax - 1) {
            return false;
        }
        out[count++] = *entry;
    }
    out[count] = NULL;
    return true;
}

inline bool hmiEnvironment(char *out[kHmiEnvironmentMax], char *preloadOut, size_t preloadSize,
                           bool includePreload = true) {
    return hmiEnvironmentFrom(environ, out, preloadOut, preloadSize, includePreload);
}

static const size_t kHmiRuntimeMax = 5;

/* What an application gets of it: the PATH, and the variables the HMI's own
 * processes load libraries and graphics with (starter.cfg: LD_LIBRARY_PATH
 * with /opt/sys/lib, LD_PRELOAD's EGL forwarders, GRAPHICS_ROOT), plus TZ.
 * Strips only our capture shim into `preloadOut` if present; returns false on
 * overflow rather than truncating. */
inline bool hmiRuntime(char **out, size_t outMax, char *preloadOut, size_t preloadSize,
                       size_t *outCount) {
    static const char *const kNames[] = {
        "LD_LIBRARY_PATH=", "LD_PRELOAD=", "GRAPHICS_ROOT=", "TZ="};
    size_t count = 0;
    if (!out || !outCount || outMax < 2) {
        return false;
    }
    out[count++] = hmiPath();
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
        const size_t nameLen = strlen(kNames[i]);
        for (char **entry = environ; entry && *entry; ++entry) {
            if (!strncmp(*entry, kNames[i], nameLen)) {
                char *chosen = *entry;
                if (i == 1) {
                    bool omitted = false, changed = false;
                    if (!preloadOut ||
                        !stripCapturePreload(*entry, preloadOut, preloadSize, &omitted, &changed)) {
                        return false;
                    }
                    if (omitted) {
                        break;
                    }
                    if (changed) {
                        chosen = preloadOut;
                    }
                }
                if (count >= outMax) {
                    return false;
                }
                out[count++] = chosen;
                break;
            }
        }
    }
    *outCount = count;
    return true;
}

/* `entry` (LD_LIBRARY_PATH=...) with the media libraries' directory after it,
 * as the car's mmlauncher.sh has it: its libasound needs liba2i_client.so from
 * there, which the HMI's path lacks (dev79: Doom's sound did not load and it
 * crashed). False, `out` untouched, when it is already there or does not fit. */
inline bool withMediaLibraries(const char *entry, char *out, size_t size) {
    static const char kMedia[] = "/opt/mm/lib";
    const char *paths = entry + strlen("LD_LIBRARY_PATH=");
    for (const char *at = strstr(paths, kMedia); at; at = strstr(at + 1, kMedia)) {
        const char end = at[sizeof(kMedia) - 1];
        if ((at == paths || at[-1] == ':') && (end == ':' || !end)) {
            return false;
        }
    }
    const size_t length = strlen(entry);
    if (length + sizeof(kMedia) + 1 > size) {
        return false;
    }
    memcpy(out, entry, length);
    out[length] = ':';
    memcpy(out + length + 1, kMedia, sizeof(kMedia));
    return true;
}

} // namespace process
} // namespace nbt

#endif
