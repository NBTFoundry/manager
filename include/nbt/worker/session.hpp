// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_SESSION_HPP
#define NBT_WORKER_SESSION_HPP

#include "nbt/core/secure.hpp"
#include "nbt/core/status.hpp"

#include <stdint.h>

namespace nbt {
namespace worker {

class Session {
public:
    static const uint64_t kTtlMs = UINT64_C(10) * 60 * 1000;

    Session();
    Session(Session &&other);
    Session &operator=(Session &&other);
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    core::Status start(uint64_t nowMs);
    void end();
    bool active(uint64_t nowMs);

    uint64_t expiresMs() const {
        return expires_ms_;
    }
    bool started() const {
        return active_;
    }
    bool uploaded() const {
        return uploaded_;
    }
    void setUploaded(bool uploaded) {
        uploaded_ = uploaded;
    }
    bool opened() const {
        return opened_;
    }
    void setOpened(bool opened) {
        opened_ = opened;
    }

private:
    uint64_t expires_ms_;
    bool active_;
    bool uploaded_;
    bool opened_;
};

} // namespace worker
} // namespace nbt

#endif
