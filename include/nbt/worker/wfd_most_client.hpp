#ifndef NBT_WORKER_WFD_MOST_CLIENT_HPP
#define NBT_WORKER_WFD_MOST_CLIENT_HPP

#include "nbt/core/unique_fd.hpp"
#include "nbt/most/service.hpp"
#include "nbt/worker/wifi_direct_most.hpp"

#include <stdint.h>
#include <sys/select.h>

namespace nbt {
namespace worker {

class WfdMostClient {
public:
    WfdMostClient();
    WfdMostClient(const WfdMostClient &) = delete;
    WfdMostClient &operator=(const WfdMostClient &) = delete;
    ~WfdMostClient();

    void initialize();
    void close();
    bool active() const {
        return active_;
    }
    int fd() const;
    void sets(fd_set *readers, int *highest) const;
    void poll(uint64_t nowMs);

    bool waitReady(uint64_t deadlineMs);
    bool queryState(uint64_t deadlineMs, uint8_t *stateOut);
    bool selectState(uint8_t targetState, uint64_t deadlineMs);
    bool queryConfig(uint64_t deadlineMs, WfdConfig *out);
    uint8_t cachedState() const {
        return wlanState_;
    }
    bool linkUpWithTarget() const {
        return link_ == NBT_MOST_LINK_UP && hasA003_;
    }
    bool waitStep(uint64_t deadlineMs);

private:
    template <typename Pred> bool waitUntil(uint64_t deadlineMs, Pred ready);
    bool sendRecord(const uint8_t *record, size_t length, uint32_t request, uint64_t deadlineMs);
    void drainMessages();
    void handleMessage(const nbt_most_wire_message &msg);

    most::MostService *service_;
    core::UniqueFd socket_;
    bool active_;
    uint8_t link_;
    bool hasA003_;
    bool hasState_;
    uint8_t wlanState_;
    bool stateError_;
    uint8_t stateErrorCode_;
    uint32_t stateSeq_;
    bool hasConfig_;
    uint32_t configSeq_;
    WfdConfig config_;
    bool selectActive_;
    uint16_t selectHandle_;
    bool selectDone_;
    bool selectOk_;
    uint8_t selectErrorCode_;
    uint32_t pendingSendReq_;
    bool pendingSendDone_;
    uint8_t pendingSendCode_;
    uint16_t nextHandle_;
    uint32_t nextRequest_;
};

} // namespace worker
} // namespace nbt

#endif
