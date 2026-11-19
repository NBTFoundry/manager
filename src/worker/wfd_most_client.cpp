#include "nbt/worker/wfd_most_client.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/core/system.hpp"
#include "nbt/log.h"
#include "nbt/most.h"
#include "nbt/most_policy.h"

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>

namespace nbt {
namespace worker {
namespace {

const char kWfdPolicy[] = "rx:A0.03.*,tx:A0.03.*,sub:A0.03.C10,sub:A0.03.C12";
const uint64_t kStepWaitMs = 20;

} // namespace

WfdMostClient::WfdMostClient()
    : service_(NULL), socket_(), active_(false), link_(NBT_MOST_LINK_DETACHED), hasA003_(false),
      hasState_(false), wlanState_(0), stateError_(false), stateErrorCode_(0), stateSeq_(0),
      hasConfig_(false), configSeq_(0), config_(), selectActive_(false), selectHandle_(0),
      selectDone_(false), selectOk_(false), selectErrorCode_(0), pendingSendReq_(0),
      pendingSendDone_(false), pendingSendCode_(0), nextHandle_(0), nextRequest_(1) {
}

WfdMostClient::~WfdMostClient() {
    close();
}

void WfdMostClient::initialize() {
    if (active_) {
        return;
    }
#ifdef NBT_TESTING
    const char *library = getenv("NBT_TEST_DSI_LIB");
    if (!library || !library[0]) {
        return;
    }
#else
    const char *library = NULL;
#endif
    nbt_most_policy policy;
    int hostFd = -1, appFd = -1;
    if (nbt_most_policy_parse(kWfdPolicy, &policy) != 0 ||
        most::MostService::socket(&hostFd, &appFd) != 0) {
        nbt_logf("Wi-Fi Direct MOST: socket initialization failed");
        return;
    }
    core::UniqueFd hostOwner(hostFd), appOwner(appFd);
    const int flags = fcntl(appOwner.get(), F_GETFL, 0);
    if (flags < 0 || fcntl(appOwner.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
        nbt_logf("Wi-Fi Direct MOST: socket flags failed");
        return;
    }
    if (nbt_most_service_start(library, "wfd", &policy, hostOwner.get(), -1) != 0) {
        nbt_logf("Wi-Fi Direct MOST: unavailable (DSI failed to start)");
        return;
    }
    (void)hostOwner.release();
    service_ = most::MostService::shared();
    if (!service_) {
        nbt_logf("Wi-Fi Direct MOST: service instance missing");
        return;
    }
    socket_ = std::move(appOwner);
    active_ = true;
}

void WfdMostClient::close() {
    config_.clear();
    socket_.reset();
    service_ = NULL;
    active_ = false;
}

int WfdMostClient::fd() const {
    return active_ ? socket_.get() : -1;
}

void WfdMostClient::sets(fd_set *readers, int *highest) const {
    const int s = fd();
    if (readers && highest && s >= 0 && s < FD_SETSIZE) {
        FD_SET(s, readers);
        if (s > *highest) {
            *highest = s;
        }
    }
}

void WfdMostClient::poll(uint64_t nowMs) {
    if (!active_ || !service_) {
        return;
    }
    (void)service_->poll(nowMs);
    drainMessages();
}

void WfdMostClient::drainMessages() {
    uint8_t record[NBT_MOST_WIRE_RECORD_MAX];
    for (ssize_t n; socket_.valid() && (n = recv(socket_.get(), record, sizeof(record), 0)) > 0;) {
        const size_t len = static_cast<size_t>(n);
        const int type = nbt_most_wire_type(record, len);
        if (type == NBT_MOST_WIRE_STATE) {
            nbt_most_wire_state state;
            if (!nbt_most_wire_decode_state(record, len, &state)) {
                link_ = state.link;
                hasA003_ = link_ == NBT_MOST_LINK_UP ? hasA003_ : false;
            }
        } else if (type == NBT_MOST_WIRE_REGISTRY) {
            nbt_most_wire_entry entries[NBT_MOST_WIRE_REGISTRY_MAX];
            const long count =
                nbt_most_wire_decode_registry(record, len, entries, NBT_MOST_WIRE_REGISTRY_MAX);
            hasA003_ = false;
            for (long i = 0; i < count && i < static_cast<long>(NBT_MOST_WIRE_REGISTRY_MAX); ++i) {
                if (entries[i].key == kMostKeyWfd && entries[i].address >= 0x0100u &&
                    entries[i].address <= 0x4fffu) {
                    hasA003_ = true;
                    break;
                }
            }
        } else if (type == NBT_MOST_WIRE_RESULT) {
            nbt_most_wire_result res;
            if (!nbt_most_wire_decode_result(record, len, &res) && pendingSendReq_ &&
                res.request == pendingSendReq_) {
                pendingSendCode_ = res.code;
                pendingSendDone_ = true;
            }
        } else if (type == NBT_MOST_WIRE_MESSAGE) {
            nbt_most_wire_message msg;
            if (!nbt_most_wire_decode_message(record, len, &msg)) {
                handleMessage(msg);
            }
        }
        core::secureClear(record, sizeof(record));
    }
}

void WfdMostClient::handleMessage(const nbt_most_wire_message &msg) {
    WfdStateReply stateReply;
    WfdSelectReply selReply;
    WfdConfig parsed;
    if (decodeWfdState(msg, &stateReply)) {
        stateError_ = stateReply.op != kMostOpStatus;
        if (!stateError_) {
            hasState_ = true;
            wlanState_ = stateReply.state;
        } else {
            stateErrorCode_ = stateReply.errorCode;
        }
        ++stateSeq_;
    } else if (decodeWfdSelect(msg, &selReply)) {
        const bool ours = selectActive_ && selReply.handle == selectHandle_;
        if (ours && (selReply.op == kMostOpResultAck || selReply.op == kMostOpErrorAck)) {
            if (selReply.op == kMostOpResultAck && selReply.hasState) {
                hasState_ = true;
                wlanState_ = selReply.state;
                ++stateSeq_;
            }
            selectErrorCode_ = selReply.errorCode;
            selectOk_ = selReply.op == kMostOpResultAck;
            selectDone_ = true;
        } else if (!ours) {
            nbt_logf("Wi-Fi Direct: SelectWLANState3 answer op %X for handle %u, not ours",
                     static_cast<unsigned>(selReply.op), static_cast<unsigned>(selReply.handle));
        }
    } else if (decodeWfdConfig(msg, &parsed) && config_.copyFrom(parsed)) {
        hasConfig_ = true;
        ++configSeq_;
    } else if (msg.fblock == kMostFblockWlan && msg.inst == kMostInstWfd &&
               msg.fkt == kFktSelectWlanState3 && msg.op != kMostOpStartResultAck) {
        // StartResultAck is our own request, seen again (dev67's debug log).
        nbt_logf("Wi-Fi Direct: SelectWLANState3 answer op %X (%u bytes) not understood",
                 static_cast<unsigned>(msg.op), static_cast<unsigned>(msg.length));
    }
    parsed.clear();
}

bool WfdMostClient::waitStep(uint64_t deadlineMs) {
    const core::Result<uint64_t> now = core::monotonicMs();
    if (!now.get() || now.value() >= deadlineMs) {
        return false;
    }
    const uint64_t remaining = deadlineMs - now.value();
    struct pollfd pfd = {socket_.get(), POLLIN, 0};
    (void)::poll(&pfd, 1, static_cast<int>(remaining < kStepWaitMs ? remaining : kStepWaitMs));
    const core::Result<uint64_t> after = core::monotonicMs();
    if (!after.get()) {
        return false;
    }
    poll(after.value());
    return after.value() < deadlineMs;
}

template <typename Pred> bool WfdMostClient::waitUntil(uint64_t deadlineMs, Pred ready) {
    while (!ready()) {
        if (!waitStep(deadlineMs)) {
            return ready();
        }
    }
    return true;
}

bool WfdMostClient::sendRecord(const uint8_t *record, size_t length, uint32_t request,
                               uint64_t deadlineMs) {
    if (!socket_.valid() || !service_) {
        return false;
    }
    pendingSendReq_ = request;
    pendingSendDone_ = false;
    pendingSendCode_ = 0;
    if (send(socket_.get(), record, length, 0) != static_cast<ssize_t>(length)) {
        pendingSendReq_ = 0;
        return false;
    }
    const core::Result<uint64_t> now = core::monotonicMs();
    if (now.get()) {
        poll(now.value());
    }
    const bool done = waitUntil(deadlineMs, [this]() { return pendingSendDone_; });
    pendingSendReq_ = 0;
    return done && pendingSendCode_ == NBT_MOST_SENT;
}

bool WfdMostClient::waitReady(uint64_t deadlineMs) {
    const bool ok = waitUntil(deadlineMs, [this]() { return linkUpWithTarget(); });
    if (!ok) {
        nbt_logf("Wi-Fi Direct: MOST FBlock A0.03 unavailable (link=%u)",
                 static_cast<unsigned>(link_));
    }
    return ok;
}

bool WfdMostClient::queryState(uint64_t deadlineMs, uint8_t *stateOut) {
    uint8_t record[NBT_MOST_WIRE_RECORD_MAX];
    size_t length = 0;
    const uint32_t req = nextRequest_++;
    const uint32_t seqBefore = stateSeq_;
    if (!encodeWfdGet(kFktWlanState3, req, record, sizeof(record), &length) ||
        !sendRecord(record, length, req, deadlineMs) ||
        !waitUntil(deadlineMs, [this, seqBefore]() { return stateSeq_ != seqBefore; })) {
        nbt_logf("Wi-Fi Direct: WLANState3 request failed or timed out");
        return false;
    }
    if (stateError_) {
        nbt_logf("Wi-Fi Direct: WLANState3 returned error %u",
                 static_cast<unsigned>(stateErrorCode_));
        return false;
    }
    if (stateOut) {
        *stateOut = wlanState_;
    }
    return true;
}

bool WfdMostClient::selectState(uint8_t targetState, uint64_t deadlineMs) {
    uint8_t record[NBT_MOST_WIRE_RECORD_MAX];
    size_t length = 0;
    if (!++nextHandle_) {
        ++nextHandle_;
    }
    const uint16_t handle = nextHandle_;
    const uint32_t req = nextRequest_++;
    if (!encodeWfdSelectState(handle, targetState, req, record, sizeof(record), &length)) {
        return false;
    }
    selectActive_ = true;
    selectHandle_ = handle;
    selectDone_ = false;
    selectOk_ = false;
    selectErrorCode_ = 0;
    // The car also reports WLANState3 when the group changes; that report is
    // enough when its answer to the select does not come or is not understood.
    const uint32_t seqBefore = stateSeq_;
    const bool sent = sendRecord(record, length, req, deadlineMs);
    const bool done = sent && waitUntil(deadlineMs, [this, seqBefore, targetState]() {
                          return selectDone_ || (stateSeq_ != seqBefore && hasState_ &&
                                                 !stateError_ && wlanState_ == targetState);
                      });
    if (done && !selectDone_) {
        nbt_logf("Wi-Fi Direct: WLANState3 now %u; SelectWLANState3 not answered",
                 static_cast<unsigned>(targetState));
        selectOk_ = true;
    }
    selectActive_ = false;
    if (!done || !selectOk_) {
        nbt_logf("Wi-Fi Direct: SelectWLANState3(%u) %s (error %u)",
                 static_cast<unsigned>(targetState), !done ? "timed out" : "rejected",
                 static_cast<unsigned>(selectErrorCode_));
        return false;
    }
    return true;
}

bool WfdMostClient::queryConfig(uint64_t deadlineMs, WfdConfig *out) {
    uint8_t record[NBT_MOST_WIRE_RECORD_MAX];
    size_t length = 0;
    const uint32_t req = nextRequest_++;
    const uint32_t seqBefore = configSeq_;
    if (!encodeWfdGet(kFktWlanConfiguration, req, record, sizeof(record), &length) ||
        !sendRecord(record, length, req, deadlineMs) ||
        !waitUntil(deadlineMs, [this, seqBefore]() { return configSeq_ != seqBefore; })) {
        nbt_logf("Wi-Fi Direct: WLANConfiguration request failed or timed out");
        return false;
    }
    return hasConfig_ && (!out || out->copyFrom(config_));
}

} // namespace worker
} // namespace nbt
