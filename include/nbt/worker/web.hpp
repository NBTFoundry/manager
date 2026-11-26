// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_WEB_HPP
#define NBT_WORKER_WEB_HPP

#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/worker/http.hpp"
#include "nbt/worker/phone_api.hpp"
#include "nbt/worker/worker.hpp"
#include <openssl/sha.h>

#include <stdint.h>
#include <sys/select.h>

namespace nbt {
namespace worker {

static const unsigned kWebClients = 4;
/* The car's two Wi-Fi networks: the Wi-Fi Direct group, then the CarPlay hotspot. */
static const unsigned kWebListeners = 2;
static const uint64_t kConnectionTimeoutMs = 15000;

class WebConnection {
public:
    /* Wait: a log reply waits for sloginfo (poll). */
    enum class Phase { Header, Body, Wait, Reply };

    WebConnection();
    WebConnection(const WebConnection &) = delete;
    WebConnection &operator=(const WebConnection &) = delete;

    bool valid() const {
        return fd_.valid();
    }
    int fd() const {
        return fd_.get();
    }
    Phase phase() const {
        return phase_;
    }
    uint64_t lastMs() const {
        return lastMs_;
    }

    void acceptClient(core::UniqueFd fd, uint64_t nowMs);
    void closeClient(PhoneApi &api);
    core::Status readClient(const char *host, PhoneApi &api, uint64_t nowMs);
    bool writeReply(uint64_t nowMs, bool *completed);
    core::Status poll(PhoneApi &api, uint64_t nowMs);

private:
    void clear();
    /* `bodyText`, then the log files' bytes when `files` is set. */
    core::Status reply(PhoneApi &api, uint64_t now, int status, const char *bodyText,
                       bool files = false);
    bool refill();
    core::Status body(PhoneApi &api, const unsigned char *data, size_t size, uint64_t nowMs);

    core::UniqueFd fd_;
    core::UniqueFd uploadFd_;
    Phase phase_;
    unsigned char input_[http::kHeaderMax];
    size_t used_;
    http::Request request_;
    uint64_t received_;
    uint64_t lastMs_;
    char shortBody_[2049];
    SHA256_CTX hash_;
    uint64_t startedMs_;
    char reply_[8192];
    size_t replySize_;
    size_t replySent_;
    LogReply log_;
    unsigned file_;
};

class WebServer {
public:
    explicit WebServer(Worker &worker);
    PhoneApi &api() {
        return api_;
    }
    core::Status initialize(bool door = false) {
        return api_.initialize(door) ? core::Status()
                                     : core::Status::error(5, "phone API initialization failed");
    }
    WebServer(const WebServer &) = delete;
    WebServer &operator=(const WebServer &) = delete;

    /* The first listener (the pairing session's, named in SESSION's reply). */
    core::Status listen(const char *interfaceName, unsigned port);
    /* Another, on `interfaceName`, once that interface has an address, whether or not the
     * first one is up: a phone another app joined to the CarPlay hotspot reaches the manager
     * there (docs/android-auto-wifi.md). Closed by close() with the first one. */
    core::Status listenAlso(const char *interfaceName, unsigned port);
    bool listeningOn(const char *interfaceName) const;
    void close(Worker &worker);
    void dropClients(Worker &worker);
    void sets(fd_set *readers, fd_set *writers, int *highest);
    void handle(Worker &worker, const fd_set *readers, const fd_set *writers, uint64_t nowMs);
    bool listening() const {
        return listeners_[0].fd.valid();
    }
    const char *host() const {
        return listeners_[0].host;
    }

private:
    struct Listener {
        core::UniqueFd fd;
        bool firewall;
        char interface[16];
        uint32_t address;
        uint16_t port;
        char host[128];
        char bssid[18];
        char label[32];
    };
    core::Status open(unsigned index, const char *interfaceName, unsigned port);
    void openFirewall(Listener &listener);
    void accept(unsigned index, uint64_t nowMs);

    PhoneApi api_;
    Listener listeners_[kWebListeners];
    uint64_t firewallMs_;
    WebConnection clients_[kWebClients];
    /* The listener each client came through: its address is the one requests sign. */
    unsigned via_[kWebClients];
};

} // namespace worker
} // namespace nbt

#endif
