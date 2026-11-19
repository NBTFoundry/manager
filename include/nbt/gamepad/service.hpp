// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GAMEPAD_SERVICE_HPP
#define NBT_GAMEPAD_SERVICE_HPP
#include "nbt/core/mutex.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/fs.h"
#include "nbt/gamepad/shared_state.hpp"
#include "nbt/gamepad/transport.hpp"
#include <pthread.h>
namespace nbt {
namespace gamepad {
/* The pads' one USB owner, the manager GUI: open for its whole life (no
 * hand-over between processes: on the car a pad reopened right after one
 * failed GET_CONFIGURATION, 2026-11-18). Each state goes to the GUI's socket
 * and to the shared record the application hosts relay (relay.hpp). */
class Service {
public:
    Service();
    ~Service();
    static bool socket(int &host, int &child);
    /* Takes fd and transport ownership, including on failure. `statePath`
     * NULL: nothing published. */
    bool start(nbt_fs *fs, int fd, std::unique_ptr<UsbTransport> usb, const char *statePath);
    void stop();
    /* For the manager's Status page; any thread. */
    struct Report {
        bool running;     // the service thread runs
        bool owner;       // holds the pads: the ownership lock and io-usb
        bool lockRefused; // another process holds the ownership lock
        Family family;    // the open pad; Unsupported: none
        unsigned vendor, product;
        UsbProblem problem;
    };
    Report report() const;

private:
    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;
    static void *entry(void *self);
    void run();
    bool acquire();
    void disconnect();
    void deliver(uint64_t now);
    void warn(unsigned bit, bool ok, const char *step);
    void refresh(Family family);
    static const unsigned kOwnershipWarned = 2u;
    mutable core::Mutex mutex_;
    core::UniqueFd socket_, lock_;
    std::unique_ptr<UsbTransport> usb_;
    nbt_fs *fs_;
    pthread_t thread_;
    bool started_, stopping_, connected_;
    uint64_t retryAt_, sentAt_;
    uint32_t sequence_;
    StateWriter published_;
    unsigned warned_;
    nbt_gamepad_wire_state state_;
    GipSession gip_;
    Report report_; // under mutex_
};
} // namespace gamepad
} // namespace nbt
#endif
