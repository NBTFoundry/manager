// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MOST_MOST_SESSION_HPP
#define NBT_MOST_MOST_SESSION_HPP

#include "nbt/core/mutex.hpp"
#include "nbt/most/codec.hpp"
#include "nbt/most/subscriptions.hpp"
#include "nbt/most_policy.h"
#include "nbt/most_service.h"
#include "nbt/most_wire.h"

namespace nbt {
namespace most {

enum ClientReplyStatus { ClientDuplicate, ClientRefusedShort, ClientRefused, ClientAccepted };

class MostSession {
public:
    MostSession();

    bool configure(const char *app, const nbt_most_policy *rules);
    const char *clientName() const {
        return clientName_;
    }
    const nbt_most_policy &policy() const {
        return policy_;
    }

    void noteDrop();
    uint32_t droppedNow();
    bool updateLink(uint8_t value, nbt_most_wire_state *stateOut);
    void resetAttachment();
    void resetDetachment();

    ClientReplyStatus acceptClientReply(const unsigned char *bytes, uint32_t length,
                                        uint32_t *idOut);
    uint8_t nextCommLink(uint32_t commValue);
    long updateRegistry(const unsigned char *bytes, uint32_t length, uint8_t *recordOut,
                        size_t recordCap);
    long encodeForwarded(const nbt_most_message *message, uint8_t flags, uint8_t *recordOut,
                         size_t recordCap);
    uint8_t prepareSend(const uint8_t *bytes, size_t length, uint64_t now,
                        const unsigned char **payloadOut, size_t *payloadLenOut);
    long prepareSubscription(uint64_t now, const unsigned char **payloadOut, bool clear = false);
    /* While paused, messages for the application are discarded, not queued. */
    void setPaused(bool paused);
    bool paused();

private:
    bool takeToken(uint64_t now);
    uint16_t destination(uint8_t fblock, uint8_t inst, uint16_t wanted) const;

    core::Mutex lock_;
    nbt_most_policy policy_;
    Subscriptions subscriptions_;
    char clientName_[65];
    uint8_t linkState_;
    bool answered_;
    bool commOkSeen_;
    uint32_t clientId_;
    uint16_t device_;
    uint32_t dropped_;
    bool paused_;
    size_t registryCount_;
    nbt_most_wire_entry registry_[NBT_MOST_WIRE_REGISTRY_MAX];
    uint32_t registryHash_;
    bool registryKnown_;
    uint64_t tokens_;
    uint64_t refilledMs_;
    bool refillStarted_;
    unsigned refusalsLogged_;
    unsigned char dsiPayload_[4 + 32 + NBT_MOST_WIRE_DATA_MAX];
};

} // namespace most
} // namespace nbt

#endif
