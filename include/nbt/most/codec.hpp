// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MOST_CODEC_HPP
#define NBT_MOST_CODEC_HPP

#include "nbt/most.h"

namespace nbt {
namespace most {

enum JoinStatus { JoinError = -1, JoinIncomplete = 0, JoinComplete = 1 };

class Codec {
public:
    static int parseAny(const unsigned char *payload, size_t length, nbt_most_message *message);
    static int parse(const unsigned char *payload, size_t length, nbt_most_message *message);
    static int clientReply(const unsigned char *payload, size_t length, uint32_t *client,
                           uint16_t *device);
    static int clientId(const unsigned char *payload, size_t length, uint32_t *client);
    static long processMessage(uint32_t client, uint16_t src, uint16_t dst, uint8_t fblock,
                               uint8_t inst, uint16_t fkt, uint8_t op, uint32_t tag,
                               const unsigned char *data, size_t length, unsigned char *out,
                               size_t size);
    static long registry(const unsigned char *payload, size_t length,
                         nbt_most_registry_entry *entries, size_t max);
    static long registerClient(const char *name, unsigned char *out, size_t size);
    static long registerControls(uint32_t client, const uint16_t *keys, size_t count,
                                 unsigned char *out, size_t size);
    static long unregisterControls(uint32_t client, const uint16_t *keys, size_t count,
                                   unsigned char *out, size_t size);
};

class Joiner {
public:
    Joiner();
    void reset();
    JoinStatus join(const nbt_most_message *segment, nbt_most_class kind, nbt_most_message *out);
    unsigned lost() const {
        return state_.lost;
    }

private:
    nbt_most_joiner state_;
};

} // namespace most
} // namespace nbt

#endif
