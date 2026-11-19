#ifndef NBT_GUI_WORKER_CLIENT_HPP
#define NBT_GUI_WORKER_CLIENT_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"

#include <sys/types.h>

namespace nbt {
namespace gui {

class WorkerClient {
public:
    WorkerClient();
    ~WorkerClient();
    WorkerClient(WorkerClient &&other);
    WorkerClient &operator=(WorkerClient &&other);
    WorkerClient(const WorkerClient &) = delete;
    WorkerClient &operator=(const WorkerClient &) = delete;

    /* Takes over the launcher's channel to a running worker and says HELLO.
     * The worker is not this process's child: it is never signalled. */
    core::Status attach(int fd);
    core::Status request(const char *command, core::Span<char> reply, unsigned timeoutMs);
    void close();
    void terminate();
    bool connected() const {
        return fd_.valid();
    }
    pid_t pid() const {
        return pid_;
    }
#ifdef NBT_TESTING
    core::Status spawnTest(const char *path, const char *root);
#endif

private:
    core::Status handshake();
#ifdef NBT_TESTING
    core::Status spawnWorker(const char *path, const char *testRoot);
#endif
    core::Status fail(int code);

    core::UniqueFd fd_;
    pid_t pid_;
};

} // namespace gui
} // namespace nbt

#endif
