// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_DIAG_SERVICE_HPP
#define NBT_PROCESS_DIAG_SERVICE_HPP

/* A host's part of capability `diag` (bridge 1.8, docs/diag-bridge.md): on
 * its own thread, it answers the application's requests (include/nbt/diag_wire.h)
 * after checking them against its signed policy, as the car's one tester on
 * the central gateway (HSFZ). The gateway is held per lease: the manager's
 * lock and the loopback lock other testers on the head unit use, released
 * when the application pauses or after a second when another waits. A clear
 * waits for the car to stand still (car data driving.speedActual 0). */

#include "nbt/diag/hsfz_tester.hpp"
#include "nbt/diag_policy.h"
#include "nbt/fs.h"

#include <pthread.h>
#include <stdint.h>

namespace nbt {
namespace process {

class DiagService {
public:
    DiagService();
    ~DiagService();
    /* Takes `fd`, the host's end of the application's socket; `carDataDir`
     * holds the car data the plugin publishes. */
    bool start(nbt_fs *fs, const char *app, const nbt_diag_policy &policy, int fd,
               const char *carDataDir);
    void stop();

private:
    DiagService(const DiagService &);
    DiagService &operator=(const DiagService &);
    static void *main(void *self);
    void loop();
    void answer(const uint8_t *data, size_t size, uint64_t now);
    void reply(uint8_t ecu, uint16_t id, uint8_t status, uint8_t transport, const uint8_t *uds,
               size_t size);
    bool parked() const;
    bool lease(uint64_t now);
    void unlease();
    bool waiterSeen();
    /* The gateway did not answer: logs what the head unit knows of its
     * wired links and looks for the gateway on them (diag_gateway.cpp). True
     * when one answered: connected, used and remembered from then on. */
    bool findGateway();
    int tryGateway(uint32_t address, uint16_t port, const char *why, bool verbose);
    /* The gateway a search found earlier (state/manager/diag-gateway). */
    void remember();
    bool remembered(char *gateway, size_t size, uint8_t *tester) const;

    nbt_fs *fs_;
    char app_[49];
    char dir_[1024];
    nbt_diag_policy policy_;
    int fd_, wake_[2], lockFd_, loopFd_;
    pthread_t thread_;
    bool running_;
    uint64_t leasedMs_, lastMs_, failLogMs_;
    uint32_t ip_;
    uint16_t port_;
    diag::HsfzTester hsfz_;
};

} // namespace process
} // namespace nbt

#endif
