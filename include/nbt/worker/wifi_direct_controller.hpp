#ifndef NBT_WORKER_WIFI_DIRECT_CONTROLLER_HPP
#define NBT_WORKER_WIFI_DIRECT_CONTROLLER_HPP

#include "nbt/core/secure.hpp"
#include "nbt/worker/wfd_health.hpp"
#include "nbt/worker/wfd_most_client.hpp"
#include "nbt/worker/wifi_direct.hpp"

#include <stdint.h>
#include <sys/select.h>

namespace nbt {
namespace worker {

struct ReadyWifiCredentials {
    ReadyWifiCredentials() : managed(false), ssid(), pass(), source(NULL) {
        ssid[0] = 0;
    }
    ReadyWifiCredentials(const ReadyWifiCredentials &) = delete;
    ReadyWifiCredentials &operator=(const ReadyWifiCredentials &) = delete;
    ~ReadyWifiCredentials() {
        clear();
    }
    void clear() {
        managed = false;
        memset(ssid, 0, sizeof(ssid));
        pass.wipe();
        source = NULL;
    }

    bool managed;
    char ssid[kWifiSsidMax + 1];
    core::SecureString<kWifiPassMax> pass;
    const char *source;
};

class WifiDirectController {
public:
    WifiDirectController();
    WifiDirectController(const WifiDirectController &) = delete;
    WifiDirectController &operator=(const WifiDirectController &) = delete;
    ~WifiDirectController();

    void initialize();
    int fd() const;
    void sets(fd_set *readers, int *highest) const;
    void poll(uint64_t nowMs);

    bool requiredForInterface(const char *iface) const;
    bool enabledByUs() const {
        return enabledByUs_;
    }

    bool prepareSession(const char *iface, uint64_t nowMs, ReadyWifiCredentials *out);
    void restore(uint64_t nowMs);
    void shutdown();

private:
    WfdMostClient client_;
    WfdHealth health_;
    bool unavailableLogged_;
    bool enabledByUs_;
};

} // namespace worker
} // namespace nbt

#endif
