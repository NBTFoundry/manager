// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/http.hpp"
#include "support/check.hpp"
#include <stdio.h>
#include <string.h>
using namespace nbt::worker::http;
static ParseResult parse(const char *text, Request &request) {
    return parseRequest(
        nbt::core::Span<const uint8_t>(reinterpret_cast<const uint8_t *>(text), strlen(text)),
        &request);
}
static bool run() {
    Request request;
    char text[2048];
    const char auth[] =
        "X-NBT-Key: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n"
        "X-NBT-Nonce: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\r\n"
        "X-NBT-Signature: 3006020101020101\r\n";
    TEST_CHECK(parse("GET /v1/nonce HTTP/1.1\r\nHost: car\r\n\r\n", request) == Complete);
    TEST_CHECK(parse("GET /v1/nonce HTTP/1.1\r\nHost: car\r\n", request) == Incomplete);
    TEST_CHECK(parse("GET / HTTP/1.1\r\nHost: car\r\n\r\n", request) == Invalid);
    TEST_CHECK(parse("GET /v1/apps HTTP/1.1\r\nHost: car\r\n\r\n", request) == Invalid);
    snprintf(text, sizeof(text), "GET /v1/apps HTTP/1.1\r\nHost: car\r\n%s\r\n", auth);
    TEST_CHECK(parse(text, request) == Complete && request.route == Apps &&
               strlen(request.keyId) == 64);
    const char *invalid[] = {"Content-Length: 1\r\n",
                             "Host: other\r\n",
                             "Transfer-Encoding: chunked\r\n",
                             "Authorization: Bearer secret\r\n",
                             "Origin: http://car\r\n",
                             "X-NBT-Key: a\r\n",
                             "X-NBT-Nonce: a\r\n",
                             "X-NBT-Signature: aa\r\n",
                             "Content-Encoding: gzip\r\n",
                             " folded: x\r\n",
                             "Upgrade: websocket\r\n",
                             "Expect: 100-continue\r\n"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        snprintf(text, sizeof(text), "GET /v1/apps HTTP/1.1\r\nHost: car\r\n%s%s\r\n", auth,
                 invalid[i]);
        TEST_CHECK(parse(text, request) == Invalid);
    }
    snprintf(text, sizeof(text),
             "POST /v1/upload HTTP/1.1\r\nHost: car\r\n%sContent-Length: 99\r\nContent-Type: "
             "application/octet-stream\r\n\r\n",
             auth);
    TEST_CHECK(parse(text, request) == Complete && request.contentLength == 99);
    snprintf(text, sizeof(text),
             "POST /v1/settings/set HTTP/1.1\r\nHost: car\r\n%sContent-Length: "
             "2049\r\nContent-Type: text/plain\r\n\r\n",
             auth);
    TEST_CHECK(parse(text, request) == Invalid);
    snprintf(text, sizeof(text),
             "POST /v1/apps HTTP/1.1\r\nHost: car\r\n%sContent-Length: 0\r\n\r\n", auth);
    TEST_CHECK(parse(text, request) == Invalid);
    // The logs: signed text requests, like the settings.
    snprintf(text, sizeof(text),
             "POST /v1/logs/app HTTP/1.1\r\nHost: car\r\n%sContent-Length: 5\r\n"
             "Content-Type: text/plain\r\n\r\n",
             auth);
    TEST_CHECK(parse(text, request) == Complete && request.route == LogsApp);
    snprintf(text, sizeof(text),
             "POST /v1/logs/system HTTP/1.1\r\nHost: car\r\n%sContent-Length: 0\r\n"
             "Content-Type: text/plain\r\n\r\n",
             auth);
    TEST_CHECK(parse(text, request) == Complete && request.route == LogsSystem);
    TEST_CHECK(!strcmp(path(LogsSystem), "/v1/logs/system"));
    TEST_CHECK(!strcmp(path(LogsClear), "/v1/logs/clear"));
    TEST_CHECK(!strcmp(path(LogsManager), "/v1/logs/manager"));
    snprintf(text, sizeof(text), "GET /v1/logs/app HTTP/1.1\r\nHost: car\r\n%s\r\n", auth);
    TEST_CHECK(parse(text, request) == Invalid);
    // The installed manager: a GET with no body (PROTOCOL.md).
    snprintf(text, sizeof(text), "GET /v1/manager HTTP/1.1\r\nHost: car\r\n%s\r\n", auth);
    TEST_CHECK(parse(text, request) == Complete && request.route == Manager);
    TEST_CHECK(!strcmp(path(Manager), "/v1/manager"));
    snprintf(text, sizeof(text),
             "POST /v1/manager HTTP/1.1\r\nHost: car\r\n%sContent-Length: 0\r\n"
             "Content-Type: text/plain\r\n\r\n",
             auth);
    TEST_CHECK(parse(text, request) == Invalid);
    snprintf(text, sizeof(text),
             "POST /v1/logs/app HTTP/1.1\r\nHost: car\r\nContent-Length: 5\r\n"
             "Content-Type: text/plain\r\n\r\n");
    TEST_CHECK(parse(text, request) == Invalid);
    unsigned char random[8192];
    unsigned state = 1;
    for (unsigned i = 0; i < 10000; ++i) {
        const size_t n = i % sizeof(random);
        for (size_t j = 0; j < n; ++j) {
            state = state * 1664525u + 1013904223u;
            random[j] = static_cast<unsigned char>(state >> 24);
        }
        (void)parseRequest(nbt::core::Span<const uint8_t>(random, n), &request);
    }
    return true;
}
int main() {
    TEST_RUN(run);
    return 0;
}
