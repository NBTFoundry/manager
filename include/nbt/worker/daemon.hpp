#ifndef NBT_WORKER_DAEMON_HPP
#define NBT_WORKER_DAEMON_HPP

#include "nbt/core/unique_fd.hpp"
#include "nbt/process/host_control.hpp"
#include "nbt/storage/managed_root.hpp"
#include "nbt/worker/control_protocol.hpp"
#include "nbt/worker/ipc_dispatcher.hpp"
#include "nbt/worker/web.hpp"
#include "nbt/worker/wifi_direct_controller.hpp"
#include "nbt/worker/wifi_direct_job.hpp"
#include "nbt/worker/worker.hpp"

namespace nbt {
namespace worker {

class Daemon : public ControlHandler {
public:
    explicit Daemon(int controlFd);
    Daemon(const Daemon &) = delete;
    Daemon &operator=(const Daemon &) = delete;

    core::Status initialize(const char *root, const char *self);
    int run();
    core::Status command(char *line, uint64_t nowMs) override;
    bool replyPending() const override {
        return dispatcher_.replyPending();
    }

private:
    void logChanges();

    storage::ManagedRoot root_;
    process::RealChildRunner children_;
    process::RealHostControl hosts_;
    Worker worker_;
    WebServer web_;
    WifiDirectController wfd_;
    WifiDirectJob wfdJob_; // wfd_'s user: Pair a phone's preparation on its own thread
    core::UniqueFd controlOwner_;
    ControlProtocol control_;
    IpcDispatcher dispatcher_;
    State loggedState_;
    bool loggedOpened_;
    char loggedMessage_[128];
};

int doorMain(const char *root, int argc, char **argv);
int autostartMain(const char *root, const char *self, int argc, char **argv);
int hostMain(const char *root, int argc, char **argv);

} // namespace worker
} // namespace nbt

#endif
