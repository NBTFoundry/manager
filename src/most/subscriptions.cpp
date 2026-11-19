#include "nbt/most/subscriptions.hpp"

#include "nbt/most/codec.hpp"

#include <string.h>

namespace nbt {
namespace most {

Subscriptions::Subscriptions() : batches_(), count_(0) {
}

void Subscriptions::configure(const nbt_most_policy &policy) {
    count_ = 0;
    memset(batches_, 0, sizeof(batches_));
    for (unsigned i = 0; i < policy.count && i < NBT_MOST_POLICY_RULES; ++i) {
        const nbt_most_rule &rule = policy.rules[i];
        if (rule.kind != NBT_MOST_SUB) {
            continue;
        }
        const uint16_t key = (uint16_t)((rule.fblock << 8) | rule.inst);
        unsigned group = 0;
        while (group < count_ && (batches_[group].key != key || batches_[group].count == 4)) {
            ++group;
        }
        if (group == count_) {
            if (count_ == NBT_MOST_POLICY_SUBSCRIPTIONS) {
                break;
            }
            batches_[count_++].key = key;
        }
        Batch &batch = batches_[group];
        batch.functions[batch.count++] = (uint16_t)rule.fkt;
    }
    reset();
}

void Subscriptions::reset() {
    for (unsigned i = 0; i < count_; ++i) {
        batches_[i].confirmed = 0;
        batches_[i].sent = false;
        batches_[i].next = 0;
        batches_[i].delay = 1000;
    }
}

void Subscriptions::registry(const nbt_most_wire_entry *entries, size_t count) {
    for (unsigned i = 0; i < count_; ++i) {
        Batch &batch = batches_[i];
        uint16_t address = 0;
        for (size_t j = 0; j < count; ++j) {
            if (entries[j].key == batch.key && entries[j].address >= 0x100 &&
                entries[j].address <= 0x4fff) {
                address = entries[j].address;
                break;
            }
        }
        if (batch.address != address) {
            batch.address = address;
            batch.confirmed = 0;
            batch.sent = false;
            batch.next = 0;
            batch.delay = 1000;
        }
    }
}

void Subscriptions::status(uint16_t source, uint16_t key, uint16_t function) {
    for (unsigned i = 0; i < count_; ++i) {
        Batch &batch = batches_[i];
        if (batch.sent && batch.address == source && batch.key == key) {
            for (unsigned j = 0; j < batch.count; ++j) {
                if (batch.functions[j] == function) {
                    batch.confirmed |= 1u << j;
                }
            }
        }
    }
}

int Subscriptions::due(uint64_t now, bool clear) const {
    for (unsigned i = 0; i < count_; ++i) {
        const Batch &batch = batches_[i];
        if (batch.address &&
            (clear ? batch.sent
                   : (batch.confirmed != (1u << batch.count) - 1 && batch.next <= now))) {
            return (int)i;
        }
    }
    return -1;
}

long Subscriptions::prepare(unsigned group, uint32_t client, uint16_t own, uint64_t now,
                            unsigned char *out, size_t capacity, bool clear) {
    if (group >= count_) {
        return -1;
    }
    Batch &batch = batches_[group];
    unsigned char data[3 + 2 * 4];
    data[0] = clear ? 3 : 1; /* ClearFunction / SetFunction: per-client address */
    data[1] = (unsigned char)(own >> 8);
    data[2] = (unsigned char)own;
    for (unsigned j = 0; j < batch.count; ++j) {
        data[3 + j * 2] = (unsigned char)(batch.functions[j] >> 8);
        data[4 + j * 2] = (unsigned char)batch.functions[j];
    }
    const long size = Codec::processMessage(client, own, batch.address, (uint8_t)(batch.key >> 8),
                                            (uint8_t)batch.key, 1, 0, 0, data, 3 + batch.count * 2,
                                            out, capacity);
    if (size > 0) {
        batch.sent = !clear;
        batch.next = now + batch.delay;
        if (batch.delay < 64000) {
            batch.delay *= 2;
        }
    }
    return size;
}

} // namespace most
} // namespace nbt
