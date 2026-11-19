// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_HTTP_HPP
#define NBT_WORKER_HTTP_HPP
#include "nbt/core/span.hpp"
#include <stdint.h>
namespace nbt {
namespace worker {
namespace http {
static const size_t kHeaderMax = 8192;
static const uint64_t kBodyMax = UINT64_C(64) * 1024 * 1024 + 4096;
enum Method { Get, Post };
enum Route {
    Nonce,
    Apps,
    SettingsRead,
    SettingsSet,
    Wake,
    Pair,
    Upload,
    Remove,
    Status,
    LogsApp,
    LogsSystem,
    LogsClear,
    LogsManager
};
enum ParseResult { Invalid = -1, Incomplete = 0, Complete = 1 };
struct Request {
    Request() {
        clear();
    }
    void clear();
    Method method;
    Route route;
    char host[128];
    char keyId[65];
    char nonce[65];
    char signature[145];
    char contentType[96];
    uint64_t contentLength;
    size_t headerSize;
    bool hasLength;
};
const char *path(Route route);
bool routeForPath(const char *value, Route &route);
bool authValue(const char *name, const unsigned char *value, size_t size, Request &request,
               unsigned &bit);
ParseResult parseRequest(core::Span<const uint8_t> data, Request *request);
} // namespace http
} // namespace worker
} // namespace nbt
#endif
