#ifndef NBT_MOST_SUBSCRIPTIONS_HPP
#define NBT_MOST_SUBSCRIPTIONS_HPP

#include "nbt/most_policy.h"
#include "nbt/most_wire.h"

namespace nbt {
namespace most {

/* Bounded notification batches. Access is serialized by MostSession's lock.
 * Four functions per Set matches the generated iDrive Notification writer. */
class Subscriptions {
public:
    Subscriptions();
    void configure(const nbt_most_policy &policy);
    void reset();
    void registry(const nbt_most_wire_entry *entries, size_t count);
    void status(uint16_t source, uint16_t key, uint16_t function);
    int due(uint64_t now, bool clear = false) const;
    long prepare(unsigned batch, uint32_t client, uint16_t own, uint64_t now, unsigned char *out,
                 size_t capacity, bool clear = false);

private:
    struct Batch {
        uint16_t key, address, functions[4];
        unsigned count, confirmed;
        bool sent;
        uint64_t next, delay;
    };
    Batch batches_[NBT_MOST_POLICY_SUBSCRIPTIONS];
    unsigned count_;
};

} // namespace most
} // namespace nbt
#endif
