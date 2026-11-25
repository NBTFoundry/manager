// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DIAG_HSFZ_TESTER_HPP
#define NBT_DIAG_HSFZ_TESTER_HPP
/* A diagnostic tester on the car's central gateway (HSFZ over TCP 6801,
 * docs/engine-diagnostics.md): a frame is a 4-byte big-endian length (of what
 * follows the 6-byte header), a 2-byte control word, then for a diagnostic
 * message the source and target addresses and the UDS bytes. The gateway
 * echoes each request (0x0002) before the control unit's answer (0x0001) and
 * asks whether the tester is still there (0x0012, answered 0x0013). */
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace diag {

static const uint16_t kHsfzPort = 6801;
static const uint8_t kTesterAddress = 0xF4;
static const size_t kHsfzFrameMax = 4096 + 6;

/* "a.b.c.d" or "a.b.c.d:port" (port 6801 by default), the address in network order. */
bool parseGateway(const char *text, uint32_t *ip, uint16_t *port);

/* The gateway other head-unit diagnostic tools take from
 * /var/opt/hmi/data/ID5/gateway-ip.txt when it is set: its first word, an
 * address parseGateway accepts, into `out`; false when absent or invalid. */
static const char kGatewayFile[] = "/var/opt/hmi/data/ID5/gateway-ip.txt";
bool gatewayFromFile(const char *path, char *out, size_t size);

class HsfzTester {
public:
    HsfzTester();
    ~HsfzTester();
    bool connect(uint32_t ip, uint16_t port, int timeoutMs);
    void close();
    bool connected() const {
        return fd_ >= 0;
    }
    enum Result { Ok, Timeout, Broken, Refused };
    /* Sends `uds` to control unit `ecu` and waits up to `timeoutMs` for its
     * answer (positive, or 7F negative for this service); "response pending"
     * (7F xx 78) extends the wait, five times at most. */
    Result request(uint8_t ecu, const uint8_t *uds, size_t size, uint8_t *answer, size_t capacity,
                   size_t *answerSize, int timeoutMs);

private:
    HsfzTester(const HsfzTester &);
    HsfzTester &operator=(const HsfzTester &);
    bool sendAll(const uint8_t *data, size_t size);
    bool fill(int timeoutMs);
    /* The next complete frame in the buffer: its control word and payload. */
    bool nextFrame(uint16_t *control, const uint8_t **payload, size_t *size);

    int fd_;
    uint8_t buffer_[2 * kHsfzFrameMax];
    size_t length_, consumed_;
};

} // namespace diag
} // namespace nbt

#endif
