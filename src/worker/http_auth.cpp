// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/http.hpp"
#include <string.h>
namespace nbt {
namespace worker {
namespace http {
namespace {
const char *const paths[] = {
    "/v1/nonce",       "/v1/apps",         "/v1/settings/read", "/v1/settings/set", "/v1/wake",
    "/v1/pair",        "/v1/upload",       "/v1/remove",        "/v1/status",       "/v1/logs/app",
    "/v1/logs/system", "/v1/logs/clear",   "/v1/logs/manager",  "/v1/files/list",   "/v1/files/put",
    "/v1/files/data",  "/v1/files/remove", "/v1/autostart",     "/v1/manager"};
}
const char *path(Route route) {
    const unsigned i = static_cast<unsigned>(route);
    return i < sizeof(paths) / sizeof(paths[0]) ? paths[i] : NULL;
}
bool routeForPath(const char *value, Route &route) {
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        if (!strcmp(paths[i], value)) {
            route = static_cast<Route>(i);
            return true;
        }
    }
    return false;
}
bool authValue(const char *name, const unsigned char *value, size_t size, Request &request,
               unsigned &bit) {
    char *out = NULL;
    if (!strcmp(name, "x-nbt-key")) {
        bit = 32;
        out = request.keyId;
    } else if (!strcmp(name, "x-nbt-nonce")) {
        bit = 64;
        out = request.nonce;
    } else if (!strcmp(name, "x-nbt-signature")) {
        bit = 128;
        out = request.signature;
    } else {
        return true;
    }
    if (bit == 128 ? (size < 16 || size > 144 || size % 2) : size != 64) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) {
            return false;
        }
    }
    memcpy(out, value, size);
    out[size] = 0;
    return true;
}
} // namespace http
} // namespace worker
} // namespace nbt
