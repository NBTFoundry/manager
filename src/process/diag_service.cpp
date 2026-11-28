// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/diag_service.hpp"

#include "nbt/diag_wire.h"
#include "nbt/gui/car_data_relay.hpp"
#include "nbt/gui/phone_wake.hpp"
#include "nbt/log.h"
#include "nbt/storage/path_open.hpp"
#include "nbt/util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

// The gateway's usual address on the head unit's OBD link (docs/engine-diagnostics.md).
const char kGateway[] = "169.254.21.3:6801";
// The loopback lock other testers on the head unit hold while they use the gateway.
const uint16_t kLoopbackLock = 42861;
const uint64_t kIdleMs = 150, kHoldMs = 1000;
const int kLeaseWaitMs = 2000, kConnectMs = 1500;

void pauseMs(unsigned ms) {
    const struct timespec t = {static_cast<time_t>(ms / 1000),
                               static_cast<long>(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

} // namespace

DiagService::DiagService()
    : fs_(NULL), app_(), dir_(), policy_(), fd_(-1), wake_(), lockFd_(-1), loopFd_(-1), thread_(),
      running_(false), leasedMs_(0), lastMs_(0), failLogMs_(0), ip_(0), port_(0), hsfz_() {
    wake_[0] = wake_[1] = -1;
}

DiagService::~DiagService() {
    stop();
}

bool DiagService::start(nbt_fs *fs, const char *app, const nbt_diag_policy &policy, int fd,
                        const char *carDataDir) {
    stop();
    // The tests' address, else the one other diagnostic tools on this head
    // unit are set to (docs/diag-bridge.md), else the usual gateway.
#ifdef NBT_TESTING
    const char *gateway = getenv("NBT_DIAG_GATEWAY");
    const char *file = getenv("NBT_DIAG_GATEWAY_FILE");
#else
    const char *gateway = NULL, *file = NULL;
#endif
    char shared[48];
    fs_ = fs;
    hsfz_.setProtocol(diag::HsfzTester::Hsfz);
    hsfz_.setTester(diag::kTesterAddress);
    if ((!gateway || !*gateway) &&
        diag::gatewayFromFile(file && *file ? file : diag::kGatewayFile, shared, sizeof(shared))) {
        gateway = shared;
        nbt_logf("host %s: diagnostics through gateway %s (gateway-ip.txt)", app, shared);
    } else if ((!gateway || !*gateway) && fs && remembered(shared, sizeof(shared))) {
        gateway = shared;
        char tester[24];
        testerText(tester, sizeof(tester));
        nbt_logf("host %s: diagnostics through gateway %s, %s (found earlier)", app, shared,
                 tester);
    }
    if (!fs || !app || strlen(app) >= sizeof(app_) || fd < 0 || !carDataDir ||
        strlen(carDataDir) >= sizeof(dir_) ||
        !diag::parseGateway(gateway && *gateway ? gateway : kGateway, &ip_, &port_) ||
        pipe(wake_)) {
        return false;
    }
    memcpy(app_, app, strlen(app) + 1);
    memcpy(dir_, carDataDir, strlen(carDataDir) + 1);
    policy_ = policy;
    fd_ = fd;
    (void)fcntl(fd_, F_SETFD, FD_CLOEXEC);
    (void)fcntl(wake_[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(wake_[1], F_SETFD, FD_CLOEXEC);
    running_ = pthread_create(&thread_, NULL, &DiagService::main, this) == 0;
    if (!running_) {
        close(wake_[0]);
        close(wake_[1]);
        wake_[0] = wake_[1] = -1;
        fd_ = -1;
    }
    return running_;
}

void DiagService::stop() {
    if (running_) {
        const char byte = 0;
        if (write(wake_[1], &byte, 1) != 1) {
            nbt_logf("host %s: diag: cannot wake its thread (errno %d)", app_, errno);
        }
        pthread_join(thread_, NULL);
        running_ = false;
    }
    int *const fds[] = {&fd_, &wake_[0], &wake_[1]};
    for (unsigned i = 0; i < 3; ++i) {
        if (*fds[i] >= 0) {
            close(*fds[i]);
            *fds[i] = -1;
        }
    }
}

void *DiagService::main(void *self) {
    static_cast<DiagService *>(self)->loop();
    return NULL;
}

void DiagService::loop() {
    uint8_t data[NBT_DIAG_WIRE_HEADER + NBT_DIAG_WIRE_UDS_MAX + 1];
    for (;;) {
        struct pollfd ready[2] = {{fd_, POLLIN, 0}, {wake_[0], POLLIN, 0}};
        const int n = poll(ready, 2, 100);
        uint64_t now = 0;
        if ((n < 0 && errno != EINTR) || nbt_monotonic_ms(&now) || (ready[1].revents & POLLIN)) {
            break;
        }
        if (n > 0 && (ready[0].revents & POLLIN)) {
            const ssize_t size = recv(fd_, data, sizeof(data), 0);
            if (size > 0) {
                answer(data, static_cast<size_t>(size), now);
            } else if (size == 0 || (errno != EINTR && errno != EAGAIN)) {
                break; // the application's end is gone
            }
        } else if (n > 0 && (ready[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            break;
        }
        // lastMs_ may be later than `now`: it is set when the answer went out.
        if (lockFd_ >= 0 && now > lastMs_ && now - lastMs_ > kIdleMs) {
            unlease();
        }
    }
    unlease();
}

void DiagService::reply(uint8_t ecu, uint16_t id, uint8_t status, uint8_t transport,
                        const uint8_t *uds, size_t size) {
    uint8_t out[NBT_DIAG_WIRE_MAX];
    const int n = nbt_diag_reply_encode(ecu, id, status, transport, uds, size, out, sizeof(out));
    if (n > 0) {
        (void)send(fd_, out, static_cast<size_t>(n), 0);
    }
}

bool DiagService::parked() const {
    char path[1024], text[128];
    if (gui::carDataValuePath(dir_, "driving.speedActual", path, sizeof(path))) {
        return false;
    }
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const ssize_t n = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (n <= 0) {
        return false;
    }
    text[n] = 0;
    // `<sequence> <iDrive's JSON>`, as the plugin writes it.
    const char *json = strchr(text, ' ');
    unsigned speed = 1;
    return json && gui::phoneSpeed(json + 1, speed) && speed == 0;
}

bool DiagService::waiterSeen() {
    char byte = 0;
    bool seen = false;
    while (loopFd_ >= 0 && recv(loopFd_, &byte, 1, MSG_DONTWAIT) == 1) {
        seen = true;
    }
    return seen;
}

bool DiagService::lease(uint64_t now) {
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(kLoopbackLock);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (int waited = 0; waited <= kLeaseWaitMs; waited += 20) {
        if (lockFd_ < 0) {
            lockFd_ = storage::SafePathOpen::lockNonBlocking(fs_, NBT_FS_DIAG_LOCK, "manager");
        }
        if (lockFd_ >= 0 && loopFd_ < 0) {
            loopFd_ = socket(AF_INET, SOCK_DGRAM, 0);
            if (loopFd_ >= 0 &&
                (fcntl(loopFd_, F_SETFD, FD_CLOEXEC) ||
                 bind(loopFd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)))) {
                const int error = errno;
                close(loopFd_);
                loopFd_ = -1;
                if (error == EADDRINUSE) { // tell the holder someone waits, as its peers do
                    const int s = socket(AF_INET, SOCK_DGRAM, 0);
                    const char waiting = 1;
                    if (s >= 0) {
                        (void)sendto(s, &waiting, 1, 0,
                                     reinterpret_cast<const sockaddr *>(&address), sizeof(address));
                        close(s);
                    }
                }
            }
        }
        if (lockFd_ >= 0 && loopFd_ >= 0) {
            leasedMs_ = now + static_cast<uint64_t>(waited);
            return true;
        }
        pauseMs(20);
    }
    unlease();
    return false;
}

void DiagService::unlease() {
    hsfz_.close(); // the connection goes before the lease
    if (loopFd_ >= 0) {
        close(loopFd_);
        loopFd_ = -1;
    }
    if (lockFd_ >= 0) {
        close(lockFd_);
        lockFd_ = -1;
    }
}

void DiagService::answer(const uint8_t *data, size_t size, uint64_t now) {
    nbt_diag_request request;
    if (nbt_diag_request_decode(data, size, &request)) {
        const uint8_t ecu = size > 1 ? data[1] : 0;
        const uint16_t id = size > 3 ? static_cast<uint16_t>(data[2] | data[3] << 8) : uint16_t(0);
        reply(ecu, id, NBT_DIAG_MALFORMED, NBT_DIAG_VIA_NONE, NULL, 0);
        return;
    }
    lastMs_ = now;
    const unsigned kind = nbt_diag_policy_allows(&policy_, request.ecu, request.uds, request.size);
    if (!kind) {
        nbt_logf("host %s: diag request %02x to %02x refused by its policy", app_, request.uds[0],
                 request.ecu);
        reply(request.ecu, request.id, NBT_DIAG_REFUSED, NBT_DIAG_VIA_NONE, NULL, 0);
        return;
    }
    // What changes the car waits for it to stand still, unless allowed while moving.
    const bool changes = (kind & (NBT_DIAG_CLEAR | NBT_DIAG_CONTROL | NBT_DIAG_SESSION)) != 0;
    if (changes && !(kind & NBT_DIAG_DRIVE) && !parked()) {
        reply(request.ecu, request.id, NBT_DIAG_NOT_PARKED, NBT_DIAG_VIA_NONE, NULL, 0);
        return;
    }
    if (lockFd_ >= 0 && now - leasedMs_ > kHoldMs && waiterSeen()) {
        unlease(); // another tester waits: its turn first
        pauseMs(20);
    }
    if (lockFd_ < 0 && !lease(now)) {
        reply(request.ecu, request.id, NBT_DIAG_BUSY, NBT_DIAG_VIA_NONE, NULL, 0);
        return;
    }
    if (!hsfz_.connected() && !hsfz_.connect(ip_, port_, kConnectMs)) {
        // Once a minute: say what was tried, and look for the gateway.
        const bool search = !failLogMs_ || now - failLogMs_ > 60000;
        failLogMs_ = search ? now : failLogMs_;
        if (!search || !findGateway()) {
            unlease();
            reply(request.ecu, request.id, NBT_DIAG_NO_ROUTE, NBT_DIAG_VIA_NONE, NULL, 0);
            return;
        }
    }
    uint8_t answerBytes[NBT_DIAG_WIRE_REPLY_MAX];
    size_t answerSize = 0;
    const diag::HsfzTester::Result result =
        hsfz_.request(request.ecu, request.uds, request.size, answerBytes, sizeof(answerBytes),
                      &answerSize, request.timeout_ms);
    if (kind & (NBT_DIAG_CLEAR | NBT_DIAG_CONTROL)) {
        nbt_logf("host %s: diag %s %02x on %02x: %s", app_,
                 kind & NBT_DIAG_CLEAR ? "clear" : "control", request.uds[0], request.ecu,
                 result == diag::HsfzTester::Ok && answerSize && answerBytes[0] != 0x7F
                     ? "done"
                     : "not done");
    }
    if (result == diag::HsfzTester::Ok && answerSize) {
        reply(request.ecu, request.id, NBT_DIAG_OK, NBT_DIAG_VIA_HSFZ, answerBytes, answerSize);
    } else if (result == diag::HsfzTester::Timeout) {
        reply(request.ecu, request.id, NBT_DIAG_TIMEOUT, NBT_DIAG_VIA_HSFZ, NULL, 0);
    } else {
        unlease();
        reply(request.ecu, request.id, NBT_DIAG_NO_ROUTE, NBT_DIAG_VIA_HSFZ, NULL, 0);
    }
    (void)nbt_monotonic_ms(&lastMs_);
}

} // namespace process
} // namespace nbt
