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
/* DoIP (ISO 13400-2), which some gateways speak instead of HSFZ: TCP 13400,
 * a routing activation per connection, two-byte logical addresses. */
static const uint16_t kDoipPort = 13400;
/* The DoIP tester addresses tried, the first the gateway activates kept:
 * BMW's one-byte testers, then the standard on-board and external ranges. */
static const uint16_t kDoipTesters[] = {0x00F4, 0x00F2, 0x0E80, 0x0E00};
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

/* A diagnostic tester on the car's gateway: HSFZ, or DoIP once
 * setProtocol(Doip) (src/diag/doip_tester.cpp). */
class HsfzTester {
public:
    HsfzTester();
    ~HsfzTester();
    enum Protocol { Hsfz, Doip };
    void setProtocol(Protocol protocol) {
        protocol_ = protocol;
    }
    Protocol protocol() const {
        return protocol_;
    }
    /* DoIP: the tester's logical address (kDoipTesters[0] unless set), and
     * the last routing activation's response code (0x10: activated; -1: no
     * answer; 0x100: a generic header refusal). */
    void setDoipTester(uint16_t tester) {
        doipTester_ = tester;
    }
    uint16_t doipTester() const {
        return doipTester_;
    }
    int activation() const {
        return activation_;
    }
    /* Connects; for DoIP also activates routing (ECONNREFUSED when refused). */
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
    /* The HSFZ control word of the last refusal (0x0040 bad tester ... 0x00FF);
     * DoIP: 0x8300 | a diagnostic message's negative acknowledgement code,
     * 0x0100 | a generic header refusal. */
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
    // DoIP (doip_tester.cpp): the same, with the payload type.
    bool nextDoipFrame(uint16_t *type, const uint8_t **payload, size_t *size);
    bool sendDoip(uint16_t type, const uint8_t *payload, size_t size);
    bool activate(int timeoutMs);
    Result requestDoip(uint8_t ecu, const uint8_t *uds, size_t size, uint8_t *answer,
                       size_t capacity, size_t *answerSize, int timeoutMs);

    int fd_;
    int connectError_ = 0;
    uint8_t tester_ = kTesterAddress;
    uint16_t refusal_ = 0;
    Protocol protocol_ = Hsfz;
    uint16_t doipTester_ = kDoipTesters[0];
    int activation_ = -1;
    uint8_t buffer_[2 * kHsfzFrameMax];
    size_t length_, consumed_;
};

} // namespace diag
} // namespace nbt

#endif
