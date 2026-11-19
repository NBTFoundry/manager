#ifndef NBT_WORKER_CONTROL_PROTOCOL_HPP
#define NBT_WORKER_CONTROL_PROTOCOL_HPP

#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace worker {

class ControlHandler {
public:
    virtual ~ControlHandler() {
    }
    virtual core::Status command(char *line, uint64_t nowMs) = 0;
    /* A command's reply is still to come (SESSION while Wi-Fi Direct is being
     * prepared on its thread): later commands wait, so replies keep their order. */
    virtual bool replyPending() const {
        return false;
    }
};

class ControlProtocol {
public:
    explicit ControlProtocol(int fd);
    ControlProtocol(const ControlProtocol &) = delete;
    ControlProtocol &operator=(const ControlProtocol &) = delete;

    core::Status respond(const char *message);
    core::Status read(ControlHandler &handler, uint64_t nowMs);
    /* Dispatches the complete commands received, until a reply is pending. */
    core::Status process(ControlHandler &handler, uint64_t nowMs);
    bool negotiated() const {
        return negotiated_;
    }
    void setNegotiated(bool negotiated) {
        negotiated_ = negotiated;
    }
    bool quitting() const {
        return quitting_;
    }
    void quit() {
        quitting_ = true;
    }

private:
    int fd_;
    bool quitting_;
    bool negotiated_;
    char input_[1024];
    size_t inputSize_;
};

} // namespace worker
} // namespace nbt

#endif
