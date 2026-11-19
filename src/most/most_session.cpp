// SPDX-License-Identifier: MPL-2.0
#include "nbt/most/most_session.hpp"

#include "nbt/log.h"

#include <stdio.h>
#include <string.h>

namespace {

const char kClientPrefix[] = "NBTAPP_";
const unsigned kRefusalsLogged = 8;
const uint16_t kRingFirst = 0x0100;
// Include internal head-unit service nodes (e.g. 0x203F Connectivity) below the HU gateway
// (0x5000).
const uint16_t kRingLast = 0x4fff;

} // namespace

namespace nbt {
namespace most {

MostSession::MostSession()
    : lock_(), policy_(), subscriptions_(), clientName_(), linkState_(NBT_MOST_LINK_DETACHED),
      answered_(false), commOkSeen_(false), clientId_(0), device_(0), dropped_(0), paused_(false),
      registryCount_(0), registry_(), registryHash_(0), registryKnown_(false), tokens_(0),
      refilledMs_(0), refillStarted_(false), refusalsLogged_(0), dsiPayload_() {
}

bool MostSession::configure(const char *app, const nbt_most_policy *rules) {
    if (!app || !rules) {
        return false;
    }
    const int n = snprintf(clientName_, sizeof(clientName_), "%s%s", kClientPrefix, app);
    if (n < 0 || n >= (int)sizeof(clientName_)) {
        return false;
    }
    policy_ = *rules;
    subscriptions_.configure(policy_);
    return true;
}

void MostSession::noteDrop() {
    core::LockGuard guard(lock_);
    ++dropped_;
}

void MostSession::setPaused(bool paused) {
    core::LockGuard guard(lock_);
    paused_ = paused;
}

bool MostSession::paused() {
    core::LockGuard guard(lock_);
    return paused_;
}

uint32_t MostSession::droppedNow() {
    core::LockGuard guard(lock_);
    return dropped_;
}

bool MostSession::updateLink(uint8_t value, nbt_most_wire_state *stateOut) {
    core::LockGuard guard(lock_);
    const bool changed = linkState_ != value;
    if (changed) {
        subscriptions_.reset();
    }
    linkState_ = value;
    stateOut->link = value;
    if (value == NBT_MOST_LINK_UP || value == NBT_MOST_LINK_COMM_LOST) {
        stateOut->address = device_;
    } else {
        stateOut->address = 0;
    }
    stateOut->dropped = dropped_;
    return changed;
}

void MostSession::resetAttachment() {
    {
        core::LockGuard guard(lock_);
        answered_ = false;
        commOkSeen_ = false;
        clientId_ = 0;
        device_ = 0;
    }
    registryKnown_ = false;
}

void MostSession::resetDetachment() {
    core::LockGuard guard(lock_);
    answered_ = false;
    commOkSeen_ = false;
    registryCount_ = 0;
    subscriptions_.registry(NULL, 0);
}

ClientReplyStatus MostSession::acceptClientReply(const unsigned char *bytes, uint32_t length,
                                                 uint32_t *idOut) {
    uint32_t id = 0;
    uint16_t address = 0;
    core::LockGuard guard(lock_);
    if (answered_) {
        return ClientDuplicate;
    }
    const int refused = Codec::clientReply(bytes, length, &id, &address);
    if (refused) {
        if (length >= 6) {
            answered_ = true;
            return ClientRefused;
        }
        return ClientRefusedShort;
    }
    answered_ = true;
    clientId_ = id;
    device_ = address;
    *idOut = id;
    return ClientAccepted;
}

uint8_t MostSession::nextCommLink(uint32_t commValue) {
    core::LockGuard guard(lock_);
    if (commValue == NBT_MOST_COMM_OK) {
        commOkSeen_ = true;
    }
    if (!answered_ || linkState_ == NBT_MOST_LINK_REFUSED) {
        return linkState_;
    }
    if (commValue == NBT_MOST_COMM_OK || !commOkSeen_) {
        return NBT_MOST_LINK_UP;
    }
    return NBT_MOST_LINK_COMM_LOST;
}

long MostSession::updateRegistry(const unsigned char *bytes, uint32_t length, uint8_t *recordOut,
                                 size_t recordCap) {
    nbt_most_registry_entry entries[NBT_MOST_WIRE_REGISTRY_MAX];
    uint32_t hash = 2166136261u;
    const long count = Codec::registry(bytes, length, entries, NBT_MOST_WIRE_REGISTRY_MAX);
    if (count < 0) {
        nbt_logf("MOST %s: registry malformed", clientName_);
        return 0;
    }
    for (uint32_t i = 0; i < length; ++i) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    if (registryKnown_ && hash == registryHash_) {
        return 0;
    }
    registryKnown_ = true;
    registryHash_ = hash;
    const size_t kept =
        (size_t)count < NBT_MOST_WIRE_REGISTRY_MAX ? (size_t)count : NBT_MOST_WIRE_REGISTRY_MAX;
    long n = 0;
    {
        core::LockGuard guard(lock_);
        for (size_t i = 0; i < kept; ++i) {
            registry_[i].address = entries[i].first;
            registry_[i].key = entries[i].key;
        }
        registryCount_ = kept;
        subscriptions_.registry(registry_, kept);
        n = nbt_most_wire_encode_registry(registry_, kept, recordOut, recordCap);
    }
    if (kept < (size_t)count) {
        nbt_logf("MOST %s: registry of %ld entries cut to %lu", clientName_, count,
                 (unsigned long)kept);
    }
    return n;
}

long MostSession::encodeForwarded(const nbt_most_message *message, uint8_t flags,
                                  uint8_t *recordOut, size_t recordCap) {
    if (message->op == 12) {
        core::LockGuard guard(lock_);
        subscriptions_.status(message->src, (uint16_t)(message->fblock << 8 | message->inst),
                              message->fkt);
    }
    nbt_most_wire_message out;
    memset(&out, 0, sizeof(out));
    out.src = message->src;
    out.dst = message->dst;
    out.fblock = message->fblock;
    out.inst = message->inst;
    out.fkt = message->fkt;
    out.op = message->op;
    out.flags = flags;
    out.tag = message->tag;
    out.dropped = droppedNow();
    out.data = message->data;
    out.length = message->length;
    return nbt_most_wire_encode_message(NBT_MOST_WIRE_MESSAGE, &out, recordOut, recordCap);
}

bool MostSession::takeToken(uint64_t now) {
    const uint64_t cost = 1000;
    const uint64_t full = (uint64_t)NBT_MOST_SEND_RATE * cost;
    if (!refillStarted_) {
        refillStarted_ = true;
        refilledMs_ = now;
        tokens_ = full;
    }
    if (now > refilledMs_) {
        const uint64_t gained = (now - refilledMs_) * NBT_MOST_SEND_RATE;
        tokens_ = gained >= full - tokens_ ? full : tokens_ + gained;
        refilledMs_ = now;
    }
    if (tokens_ < cost) {
        return false;
    }
    tokens_ -= cost;
    return true;
}

long MostSession::prepareSubscription(uint64_t now, const unsigned char **payloadOut, bool clear) {
    core::LockGuard guard(lock_);
    const int group = subscriptions_.due(now, clear);
    if (linkState_ != NBT_MOST_LINK_UP || group < 0) {
        return 0;
    }
    if (!takeToken(now)) {
        return -1;
    }
    *payloadOut = dsiPayload_;
    return subscriptions_.prepare((unsigned)group, clientId_, device_, now, dsiPayload_,
                                  sizeof(dsiPayload_), clear);
}

uint16_t MostSession::destination(uint8_t fblock, uint8_t inst, uint16_t wanted) const {
    const uint16_t key = (uint16_t)(fblock << 8 | inst);
    for (size_t i = 0; i < registryCount_; ++i) {
        const uint16_t address = registry_[i].address;
        if (registry_[i].key == key && address >= kRingFirst && address <= kRingLast &&
            (!wanted || wanted == address)) {
            return address;
        }
    }
    return 0;
}

uint8_t MostSession::prepareSend(const uint8_t *bytes, size_t length, uint64_t now,
                                 const unsigned char **payloadOut, size_t *payloadLenOut) {
    nbt_most_wire_message message;
    if (nbt_most_wire_type(bytes, length) != NBT_MOST_WIRE_SEND ||
        nbt_most_wire_decode_message(bytes, length, &message) ||
        message.op > NBT_MOST_OP_COMMAND_MAX) {
        return NBT_MOST_BAD_REQUEST;
    }
    if (!nbt_most_policy_sends(&policy_, message.fblock, message.inst, message.fkt)) {
        if (refusalsLogged_ < kRefusalsLogged) {
            ++refusalsLogged_;
            nbt_logf("MOST %s: send to %02X.%02X.%03X not allowed", clientName_,
                     (unsigned)message.fblock, (unsigned)message.inst, (unsigned)message.fkt);
        }
        return NBT_MOST_NOT_ALLOWED;
    }
    uint32_t id = 0;
    uint16_t src = 0;
    uint16_t dst = 0;
    uint8_t state = NBT_MOST_LINK_DETACHED;
    {
        core::LockGuard guard(lock_);
        state = linkState_;
        id = clientId_;
        src = device_;
        if (state == NBT_MOST_LINK_UP) {
            dst = destination(message.fblock, message.inst, message.dst);
        }
    }
    if (state != NBT_MOST_LINK_UP) {
        return NBT_MOST_NOT_UP;
    }
    if (!dst) {
        return NBT_MOST_NO_DESTINATION;
    }
    if (!takeToken(now)) {
        return NBT_MOST_BUSY;
    }
    const long n = Codec::processMessage(id, src, dst, message.fblock, message.inst, message.fkt,
                                         message.op, message.tag, message.data, message.length,
                                         dsiPayload_, sizeof(dsiPayload_));
    if (n <= 0) {
        return NBT_MOST_BAD_REQUEST;
    }
    *payloadOut = dsiPayload_;
    *payloadLenOut = (size_t)n;
    return 0;
}

} // namespace most
} // namespace nbt
