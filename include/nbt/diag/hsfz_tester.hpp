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
/* The tester addresses a gateway may take when it refuses ours (0x0040),
 * the usual one first. */
static const uint8_t kTesterAddresses[] = {0xF4, 0xF5, 0xF1};
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
    /* Why the last connect() failed (ETIMEDOUT: no answer in time). */
    int connectError() const {
        return connectError_;
    }
    void close();
    bool connected() const {
        return fd_ >= 0;
    }
    /* The tester address requests carry (kTesterAddress unless set). */
    void setTester(uint8_t tester) {
        tester_ = tester;
    }
    uint8_t tester() const {
        return tester_;
    }
    /* The HSFZ control word of the last refusal (0x0040 bad tester ... 0x00FF). */
    uint16_t refusal() const {
        return refusal_;
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
    int connectError_ = 0;
    uint8_t tester_ = kTesterAddress;
    uint16_t refusal_ = 0;
    uint8_t buffer_[2 * kHsfzFrameMax];
    size_t length_, consumed_;
};

} // namespace diag
} // namespace nbt

#endif
