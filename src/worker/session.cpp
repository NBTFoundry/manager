#include "nbt/worker/session.hpp"
#include <errno.h>
namespace nbt {
namespace worker {
Session::Session() : expires_ms_(0), active_(false), uploaded_(false), opened_(false) {
}
Session::Session(Session &&other)
    : expires_ms_(other.expires_ms_), active_(other.active_), uploaded_(other.uploaded_),
      opened_(other.opened_) {
    other.end();
}
Session &Session::operator=(Session &&other) {
    if (this != &other) {
        expires_ms_ = other.expires_ms_;
        active_ = other.active_;
        uploaded_ = other.uploaded_;
        opened_ = other.opened_;
        other.end();
    }
    return *this;
}
core::Status Session::start(uint64_t now) {
    end();
    if (now > UINT64_MAX - kTtlMs) {
        return core::Status::error(ERANGE, "session expiry overflow");
    }
    expires_ms_ = now + kTtlMs;
    active_ = true;
    return core::Status();
}
void Session::end() {
    expires_ms_ = 0;
    active_ = uploaded_ = opened_ = false;
}
bool Session::active(uint64_t now) {
    if (active_ && now >= expires_ms_) {
        end();
    }
    return active_;
}
} // namespace worker
} // namespace nbt
