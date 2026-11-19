#ifndef NBT_MOST_SERVICE_HPP
#define NBT_MOST_SERVICE_HPP

#include "nbt/most/codec.hpp"
#include "nbt/most/controller_feed.hpp"
#include "nbt/most/most_session.hpp"

namespace nbt {
namespace most {

enum PollStatus { PollError = -1, PollIdle = 0 };

class MostService {
public:
    MostService();

    static int socket(int *host, int *app);
    int start(const char *library, const char *app, const nbt_most_policy *rules, int host,
              int controller);
    PollStatus poll(uint64_t nowMs);
    long prepareClear(uint64_t now, const unsigned char **payload) {
        return session_.prepareSubscription(now, payload, true);
    }
    int fd() const {
        return started_ ? hostFd_ : -1;
    }

    static MostService *shared();
    void setPaused(bool paused) {
        session_.setPaused(paused);
        controller_.pause(paused);
    }
    void setShown(bool shown) {
        controller_.show(shown);
    }
    void onAttached();
    void onDetached();
    void onData(uint32_t id, const unsigned char *data, uint32_t length);

private:
    void deliver(const uint8_t *data, size_t length);
    void setLink(uint8_t value);
    void onClient(const unsigned char *data, uint32_t length);
    void onCommState(const unsigned char *data, uint32_t length);
    void onRegistry(const unsigned char *data, uint32_t length);
    void forward(const nbt_most_message *message, uint8_t flags);
    void onMessage(const unsigned char *data, uint32_t length);
    uint8_t handle(const uint8_t *data, size_t length, uint64_t now);

    MostSession session_;
    ControllerFeed controller_;
    bool started_;
    int hostFd_;
    Joiner joiner_;
    uint8_t record_[NBT_MOST_WIRE_RECORD_MAX];
    uint8_t requestRecord_[NBT_MOST_WIRE_RECORD_MAX + 1];
};

} // namespace most
} // namespace nbt

#endif
