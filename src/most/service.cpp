// SPDX-License-Identifier: MPL-2.0
#include "nbt/most/service.hpp"

#include "nbt/dsi.h"
#include "nbt/log.h"
#include "nbt/most/dsi_client.hpp"

#include <errno.h>
#include <new>
#include <string.h>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>

namespace {

using nbt::most::MostService;

const unsigned kPollBatch = 64;

nbt::core::StaticMutex startLock = NBT_STATIC_MUTEX_INITIALIZER;
std::aligned_storage<sizeof(MostService), alignof(MostService)>::type serviceStorage;
MostService *serviceInstance = NULL;

void attached(void *) {
    if (serviceInstance) {
        serviceInstance->onAttached();
    }
}

void detached(void *) {
    if (serviceInstance) {
        serviceInstance->onDetached();
    }
}

void data(uint32_t id, const unsigned char *payload, uint32_t length, void *) {
    if (serviceInstance) {
        serviceInstance->onData(id, payload, length);
    }
}

uint32_t parseRequestId(const uint8_t *rec, ssize_t n) {
    if (n < 20) {
        return 0;
    }
    return (uint32_t)rec[16] | (uint32_t)rec[17] << 8 | (uint32_t)rec[18] << 16 |
           (uint32_t)rec[19] << 24;
}

} // namespace

namespace nbt {
namespace most {

MostService::MostService()
    : session_(), controller_(), started_(false), hostFd_(-1), joiner_(), record_(),
      requestRecord_() {
}

MostService *MostService::shared() {
    return serviceInstance;
}

void MostService::deliver(const uint8_t *bytes, size_t length) {
    if (hostFd_ < 0) {
        return; // `controller` only: no raw MOST channel
    }
    ssize_t n;
    do {
        n = send(hostFd_, bytes, length, 0);
    } while (n < 0 && errno == EINTR);
    if (n != (ssize_t)length) {
        session_.noteDrop();
    }
}

void MostService::setLink(uint8_t value) {
    nbt_most_wire_state state;
    uint8_t out[12];
    controller_.link(value == NBT_MOST_LINK_UP);
    if (!session_.updateLink(value, &state)) {
        return;
    }
    nbt_logf("MOST %s: link %u, address %04X", session_.clientName(), (unsigned)value,
             (unsigned)state.address);
    if (nbt_most_wire_encode_state(&state, out, sizeof(out)) > 0) {
        deliver(out, sizeof(out));
    }
}

void MostService::onAttached() {
    unsigned char payload[96];
    session_.resetAttachment();
    joiner_.reset();
    setLink(NBT_MOST_LINK_REGISTERING);
    const long length = Codec::registerClient(session_.clientName(), payload, sizeof(payload));
    if (length > 0) {
        (void)nbt_dsi_request(NBT_MOST_REQ_REGISTER_CLIENT, payload, (size_t)length);
    }
    for (size_t i = 0; i < nbt_most_subscription_count; ++i) {
        (void)nbt_dsi_notify(nbt_most_subscriptions[i]);
    }
}

void MostService::onDetached() {
    session_.resetDetachment();
    setLink(NBT_MOST_LINK_DETACHED);
}

void MostService::onClient(const unsigned char *bytes, uint32_t length) {
    unsigned char payload[96];
    uint16_t keys[NBT_MOST_POLICY_CONTROLS];
    uint32_t id = 0;
    const ClientReplyStatus status = session_.acceptClientReply(bytes, length, &id);
    if (status == ClientDuplicate || status == ClientRefusedShort) {
        return;
    }
    if (status == ClientRefused) {
        setLink(NBT_MOST_LINK_REFUSED);
        return;
    }
    (void)nbt_dsi_notify(NBT_MOST_ATTR_COMM_STATE);
    const size_t count = controller_.controls(
        keys, nbt_most_policy_controls(&session_.policy(), keys), NBT_MOST_POLICY_CONTROLS);
    if (count) {
        const long n = Codec::registerControls(id, keys, count, payload, sizeof(payload));
        if (n <= 0 || nbt_dsi_request(NBT_MOST_REQ_REGISTER_CONTROLS, payload, (size_t)n)) {
            nbt_logf("MOST %s: device controls not registered", session_.clientName());
        }
    }
    nbt_logf("MOST %s: client %lu, %lu device controls", session_.clientName(), (unsigned long)id,
             (unsigned long)count);
    setLink(NBT_MOST_LINK_UP);
}

void MostService::onCommState(const unsigned char *bytes, uint32_t length) {
    if (length < 4) {
        return;
    }
    const uint32_t value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
                           (uint32_t)bytes[3] << 24;
    setLink(session_.nextCommLink(value));
}

void MostService::onRegistry(const unsigned char *bytes, uint32_t length) {
    const long n = session_.updateRegistry(bytes, length, record_, sizeof(record_));
    if (n > 0) {
        deliver(record_, (size_t)n);
    }
}

void MostService::forward(const nbt_most_message *message, uint8_t flags) {
    if (session_.paused()) {
        return; // the application is stopped: nothing waits for it to resume
    }
    const long n = session_.encodeForwarded(message, flags, record_, sizeof(record_));
    if (n > 0) {
        deliver(record_, (size_t)n);
    }
}

void MostService::onMessage(const unsigned char *bytes, uint32_t length) {
    nbt_most_message message;
    nbt_most_message whole;
    const int kind = Codec::parseAny(bytes, length, &message);
    if (kind < 0 || kind == NBT_MOST_TX_STATUS || message.fkt > 0xfff || message.op > 15 ||
        message.length > NBT_MOST_WIRE_DATA_MAX) {
        return;
    }
    if (kind == NBT_MOST_NORMAL) {
        controller_.message(message); // before the policy: the feed is the manager's
    }
    if (hostFd_ < 0 ||
        !nbt_most_policy_receives(&session_.policy(), message.fblock, message.inst, message.fkt)) {
        return;
    }
    if (kind == NBT_MOST_NORMAL) {
        forward(&message, 0);
    } else if (kind == NBT_MOST_NO_INFO) {
        forward(&message, NBT_MOST_FLAG_NO_INFO);
    } else if (joiner_.join(&message, (nbt_most_class)kind, &whole) == JoinComplete) {
        forward(&whole, NBT_MOST_FLAG_JOINED);
    }
}

void MostService::onData(uint32_t id, const unsigned char *bytes, uint32_t length) {
    if (id == NBT_MOST_INFO_CLIENT) {
        onClient(bytes, length);
    } else if (id == NBT_MOST_ATTR_COMM_STATE) {
        onCommState(bytes, length);
    } else if (id == NBT_MOST_ATTR_REGISTRY) {
        onRegistry(bytes, length);
    } else if (id == NBT_MOST_INFO_MESSAGE) {
        onMessage(bytes, length);
    }
}

int MostService::start(const char *library, const char *app, const nbt_most_policy *rules, int host,
                       int controller) {
    if (started_ || (host < 0 && controller < 0) || !session_.configure(app, rules)) {
        return -1;
    }
    hostFd_ = host;
    controller_.start(controller, NULL);
    joiner_.reset();
    nbt_most_wire_state state = {0, 0, 0};
    uint8_t out[12];
    if (nbt_most_wire_encode_state(&state, out, sizeof(out)) > 0) {
        deliver(out, sizeof(out));
    }
    nbt_dsi_handlers handlers = {attached, detached, data, NULL};
    if (nbt_dsi_start(library, &handlers)) {
        hostFd_ = -1;
        controller_.start(-1, NULL);
        return -1;
    }
    started_ = true;
    nbt_logf("MOST %s: service started, %u rules", session_.clientName(), session_.policy().count);
    return 0;
}

uint8_t MostService::handle(const uint8_t *bytes, size_t length, uint64_t now) {
    const unsigned char *payload = NULL;
    size_t payloadLen = 0;
    const uint8_t err = session_.prepareSend(bytes, length, now, &payload, &payloadLen);
    if (err != 0) {
        return err;
    }
    return nbt_dsi_request(NBT_MOST_REQ_PROCESS_MESSAGE, payload, payloadLen) ? NBT_MOST_SEND_FAILED
                                                                              : NBT_MOST_SENT;
}

PollStatus MostService::poll(uint64_t now) {
    if (!started_ || (hostFd_ < 0 && !controller_.active())) {
        return PollError;
    }
    const unsigned char *payload = NULL;
    for (unsigned i = 0; i < NBT_MOST_POLICY_SUBSCRIPTIONS; ++i) {
        const long length = session_.prepareSubscription(now, &payload);
        if (length <= 0) {
            break;
        }
        (void)nbt_dsi_request(NBT_MOST_REQ_PROCESS_MESSAGE, payload, (size_t)length);
    }
    for (unsigned i = 0; hostFd_ >= 0 && i < kPollBatch; ++i) {
        uint8_t out[12];
        ssize_t n;
        do {
            n = recv(hostFd_, requestRecord_, sizeof(requestRecord_), 0);
        } while (n < 0 && errno == EINTR);
        if (n < 0) {
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? PollIdle : PollError;
        }
        if (n == 0) {
            return PollError;
        }
        const uint8_t code = (size_t)n > NBT_MOST_WIRE_RECORD_MAX
                                 ? NBT_MOST_BAD_REQUEST
                                 : handle(requestRecord_, (size_t)n, now);
        nbt_most_wire_result result = {parseRequestId(requestRecord_, n), code};
        if (nbt_most_wire_encode_result(&result, out, sizeof(out)) > 0) {
            deliver(out, sizeof(out));
        }
    }
    return PollIdle;
}

} // namespace most
} // namespace nbt

extern "C" {

int nbt_most_service_start(const char *library, const char *app, const nbt_most_policy *rules,
                           int host, int controller) {
    nbt::core::LockGuard lock(startLock);
    if (!serviceInstance) {
        serviceInstance = new (&serviceStorage) nbt::most::MostService();
    }
    return serviceInstance->start(library, app, rules, host, controller);
}

} // extern "C"
