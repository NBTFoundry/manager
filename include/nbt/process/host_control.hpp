// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_HOST_CONTROL_HPP
#define NBT_PROCESS_HOST_CONTROL_HPP

#include "nbt/bridge.h"
#include "nbt/core/status.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace process {

class HostControl {
public:
    virtual ~HostControl() {
    }
    static bool validApp(const char *app);
    static int setByteLock(int fd, short type, off_t byte);
    /* Starts the host's MOST client for capabilities `most` and `controller`,
     * whichever the application asks for and the manager serves. Returns the
     * application's `most` end and sets `*controller` to its `controller` end
     * (each -1 when not granted); `available` keeps only what runs. */
    static int prepareMostChannel(const char *app, const nbt_bridge_requirement &bridge,
                                  const char *served, char available[NBT_BRIDGE_CAPS_MAX + 1],
                                  int *controller);
    virtual core::Status start(const storage::ManagedRoot &root, const char *app,
                               bool automatic) = 0;
    virtual core::Result<pid_t> pid(const storage::ManagedRoot &root, const char *app) = 0;
    virtual core::Status signal(const storage::ManagedRoot &root, const char *app,
                                int signalNumber) = 0;
    virtual core::Result<bool> show(const storage::ManagedRoot &root, const char *app) = 0;
    virtual core::Status status(const storage::ManagedRoot &root, const char *app,
                                core::Span<char> text) = 0;
    /* The application's service's host (docs/service-bridge.md): its pid (0
     * when none runs), a signal to it, a start. None by default. */
    virtual core::Result<pid_t> servicePid(const storage::ManagedRoot &, const char *) {
        return core::Result<pid_t>::success(0);
    }
    virtual core::Status serviceSignal(const storage::ManagedRoot &, const char *, int) {
        return core::Status();
    }
    virtual core::Status serviceStart(const storage::ManagedRoot &, const char *) {
        return core::Status();
    }
};

class RealHostControl : public HostControl {
public:
    core::Status start(const storage::ManagedRoot &root, const char *app, bool automatic) override;
    core::Result<pid_t> pid(const storage::ManagedRoot &root, const char *app) override;
    core::Status signal(const storage::ManagedRoot &root, const char *app,
                        int signalNumber) override;
    core::Result<bool> show(const storage::ManagedRoot &root, const char *app) override;
    core::Status status(const storage::ManagedRoot &root, const char *app,
                        core::Span<char> text) override;
    core::Result<pid_t> servicePid(const storage::ManagedRoot &root, const char *app) override;
    core::Status serviceSignal(const storage::ManagedRoot &root, const char *app,
                               int signalNumber) override;
    core::Status serviceStart(const storage::ManagedRoot &root, const char *app) override;
};

} // namespace process
} // namespace nbt

#endif
