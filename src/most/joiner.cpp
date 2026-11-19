// SPDX-License-Identifier: MPL-2.0
#include "nbt/most/codec.hpp"

#include <string.h>

namespace {

bool sameHeader(const nbt_most_message &a, const nbt_most_message &b) {
    return a.dst == b.dst && a.src == b.src && a.fblock == b.fblock && a.inst == b.inst &&
           a.fkt == b.fkt && a.op == b.op && a.tag == b.tag;
}

nbt::most::JoinStatus joinSegment(nbt_most_joiner *j, const nbt_most_message *m,
                                  nbt_most_class kind, nbt_most_message *out) {
    nbt_most_join_slot *slot = NULL;
    if (!j || !m || !out || kind < NBT_MOST_SEG_FIRST || kind > NBT_MOST_SEG_LAST ||
        (m->length && !m->data)) {
        return nbt::most::JoinError;
    }
    for (unsigned i = 0; i < NBT_MOST_JOIN_SLOTS; ++i) {
        if (j->slots[i].used && sameHeader(j->slots[i].head, *m)) {
            slot = &j->slots[i];
            break;
        }
    }
    if (kind == NBT_MOST_SEG_FIRST) {
        if (!slot) {
            nbt_most_join_slot *oldest = &j->slots[0];
            for (unsigned i = 0; i < NBT_MOST_JOIN_SLOTS && !slot; ++i) {
                if (!j->slots[i].used) {
                    slot = &j->slots[i];
                } else if (j->clock - j->slots[i].stamp > j->clock - oldest->stamp) {
                    oldest = &j->slots[i];
                }
            }
            if (!slot) {
                slot = oldest;
                ++j->lost;
            }
        }
        slot->used = 1;
        slot->stamp = ++j->clock;
        slot->head = *m;
        slot->head.data = NULL;
        slot->head.length = 0;
        slot->length = 0;
    } else if (!slot) {
        ++j->lost;
        return nbt::most::JoinError;
    }
    if (m->length > NBT_MOST_JOIN_MAX - slot->length) {
        slot->used = 0;
        ++j->lost;
        return nbt::most::JoinError;
    }
    if (m->length) {
        memcpy(slot->data + slot->length, m->data, m->length);
    }
    slot->length += m->length;
    if (kind != NBT_MOST_SEG_LAST) {
        return nbt::most::JoinIncomplete;
    }
    slot->used = 0;
    *out = slot->head;
    out->variant_value = 0;
    out->data = slot->data;
    out->length = slot->length;
    return nbt::most::JoinComplete;
}

} // namespace

namespace nbt {
namespace most {

Joiner::Joiner() {
    reset();
}

void Joiner::reset() {
    memset(&state_, 0, sizeof(state_));
}

JoinStatus Joiner::join(const nbt_most_message *segment, nbt_most_class kind,
                        nbt_most_message *out) {
    return joinSegment(&state_, segment, kind, out);
}

} // namespace most
} // namespace nbt

extern "C" {

void nbt_most_join_init(nbt_most_joiner *j) {
    if (j) {
        memset(j, 0, sizeof(*j));
    }
}

int nbt_most_join(nbt_most_joiner *j, const nbt_most_message *m, nbt_most_class kind,
                  nbt_most_message *out) {
    return joinSegment(j, m, kind, out);
}

} // extern "C"
