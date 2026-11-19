// SPDX-License-Identifier: MPL-2.0
#include "nbt/sdk/most_wire.hpp"

#include <errno.h>
#include <string.h>

namespace nbt {
namespace sdk {
namespace wire {
namespace {

uint16_t get16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t get32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void put16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

void writePrefix(uint8_t *out, uint8_t recordType) {
    out[0] = NBT_MOST_WIRE_VERSION;
    out[1] = recordType;
    out[2] = 0;
    out[3] = 0;
}

template <typename T> core::Result<T> decodeFailure() {
    return core::Result<T>::failure(
        core::Status::error(core::kInvalidArgument, "invalid MOST wire record"));
}

core::Result<size_t> encodeFailure() {
    return core::Result<size_t>::failure(
        core::Status::error(core::kInvalidArgument, "invalid MOST wire value or output size"));
}

core::Result<RecordType> requireSize(bool validSize, RecordType kind) {
    return validSize ? core::Result<RecordType>::success(kind) : decodeFailure<RecordType>();
}

template <typename T> int unpack(const core::Result<T> &res, T *out) {
    if (!out || !res.get()) {
        return -1;
    }
    *out = res.value();
    return 0;
}

} // namespace

core::Result<RecordType> type(core::Span<const uint8_t> record) {
    if (!record.data() || record.size() < 4 || record[0] != NBT_MOST_WIRE_VERSION) {
        return decodeFailure<RecordType>();
    }
    switch (record[1]) {
    case NBT_MOST_WIRE_STATE:
        return requireSize(record.size() == 12, RecordState);
    case NBT_MOST_WIRE_RESULT:
        return requireSize(record.size() == 12, RecordResult);
    case NBT_MOST_WIRE_REGISTRY:
        return requireSize(record.size() >= 8, RecordRegistry);
    case NBT_MOST_WIRE_MESSAGE:
        return requireSize(record.size() >= NBT_MOST_WIRE_HEADER, RecordMessage);
    case NBT_MOST_WIRE_SEND:
        return requireSize(record.size() >= NBT_MOST_WIRE_HEADER, RecordSend);
    default:
        return decodeFailure<RecordType>();
    }
}

core::Result<State> decodeState(core::Span<const uint8_t> record) {
    core::Result<RecordType> kind = type(record);
    if (!kind.get() || kind.value() != RecordState || record[4] > NBT_MOST_LINK_UP) {
        return decodeFailure<State>();
    }
    State out = {record[4], get16(record.data() + 6), get32(record.data() + 8)};
    return core::Result<State>::success(out);
}

core::Result<size_t> decodeRegistry(core::Span<const uint8_t> record, core::Span<Entry> entries) {
    core::Result<RecordType> kind = type(record);
    if ((entries.size() && !entries.data()) || !kind.get() || kind.value() != RecordRegistry) {
        return decodeFailure<size_t>();
    }
    const uint32_t count = get32(record.data() + 4);
    if (count > NBT_MOST_WIRE_REGISTRY_MAX || record.size() != 8 + 4 * static_cast<size_t>(count)) {
        return decodeFailure<size_t>();
    }
    for (size_t i = 0; i < count && i < entries.size(); ++i) {
        entries[i].address = get16(record.data() + 8 + 4 * i);
        entries[i].key = get16(record.data() + 10 + 4 * i);
    }
    return core::Result<size_t>::success(static_cast<size_t>(count));
}

core::Result<Message> decodeMessage(core::Span<const uint8_t> record) {
    core::Result<RecordType> kind = type(record);
    if (!kind.get() || (kind.value() != RecordMessage && kind.value() != RecordSend)) {
        return decodeFailure<Message>();
    }
    const uint8_t *r = record.data();
    const size_t n = get16(r + 20);
    if (n > NBT_MOST_WIRE_DATA_MAX || record.size() != NBT_MOST_WIRE_HEADER + n || r[3] > 15 ||
        get16(r + 10) > 0xfff || get16(r + 22) != 0) {
        return decodeFailure<Message>();
    }
    Message out;
    memset(&out, 0, sizeof(out));
    out.op = r[3];
    out.fblock = r[8];
    out.inst = r[9];
    out.fkt = get16(r + 10);
    out.tag = get32(r + 12);
    out.length = n;
    out.data = r + NBT_MOST_WIRE_HEADER;
    if (kind.value() == RecordMessage) {
        out.flags = r[2];
        out.src = get16(r + 4);
        out.dst = get16(r + 6);
        out.dropped = get32(r + 16);
    } else {
        if (r[2] != 0 || get16(r + 4) != 0) {
            return decodeFailure<Message>();
        }
        out.dst = get16(r + 6);
        out.request = get32(r + 16);
    }
    return core::Result<Message>::success(out);
}

core::Result<SendResult> decodeResult(core::Span<const uint8_t> record) {
    core::Result<RecordType> kind = type(record);
    if (!kind.get() || kind.value() != RecordResult || record[8] > NBT_MOST_BUSY) {
        return decodeFailure<SendResult>();
    }
    SendResult out = {get32(record.data() + 4), record[8]};
    return core::Result<SendResult>::success(out);
}

core::Result<size_t> encodeState(const State &state, core::Span<uint8_t> output) {
    if (!output.data() || output.size() < 12 || state.link > NBT_MOST_LINK_UP) {
        return encodeFailure();
    }
    uint8_t *out = output.data();
    memset(out, 0, 12);
    writePrefix(out, NBT_MOST_WIRE_STATE);
    out[4] = state.link;
    put16(out + 6, state.address);
    put32(out + 8, state.dropped);
    return core::Result<size_t>::success(12);
}

core::Result<size_t> encodeRegistry(core::Span<const Entry> entries, core::Span<uint8_t> output) {
    if ((entries.size() && !entries.data()) || !output.data() ||
        entries.size() > NBT_MOST_WIRE_REGISTRY_MAX || output.size() < 8 + 4 * entries.size()) {
        return encodeFailure();
    }
    uint8_t *out = output.data();
    writePrefix(out, NBT_MOST_WIRE_REGISTRY);
    put32(out + 4, static_cast<uint32_t>(entries.size()));
    for (size_t i = 0; i < entries.size(); ++i) {
        put16(out + 8 + 4 * i, entries[i].address);
        put16(out + 10 + 4 * i, entries[i].key);
    }
    return core::Result<size_t>::success(8 + 4 * entries.size());
}

core::Result<size_t> encodeMessage(int recordType, const Message &m, core::Span<uint8_t> output) {
    if (!output.data() ||
        (recordType != NBT_MOST_WIRE_MESSAGE && recordType != NBT_MOST_WIRE_SEND) ||
        m.length > NBT_MOST_WIRE_DATA_MAX || (m.length && !m.data) || m.op > 15 || m.fkt > 0xfff ||
        output.size() < NBT_MOST_WIRE_HEADER + m.length) {
        return encodeFailure();
    }
    uint8_t *out = output.data();
    memset(out, 0, NBT_MOST_WIRE_HEADER);
    writePrefix(out, static_cast<uint8_t>(recordType));
    out[3] = m.op;
    out[8] = m.fblock;
    out[9] = m.inst;
    put16(out + 10, m.fkt);
    put32(out + 12, m.tag);
    put16(out + 20, static_cast<uint16_t>(m.length));
    put16(out + 6, m.dst);
    if (recordType == NBT_MOST_WIRE_MESSAGE) {
        out[2] = m.flags;
        put16(out + 4, m.src);
        put32(out + 16, m.dropped);
    } else {
        put32(out + 16, m.request);
    }
    if (m.length) {
        memcpy(out + NBT_MOST_WIRE_HEADER, m.data, m.length);
    }
    return core::Result<size_t>::success(NBT_MOST_WIRE_HEADER + m.length);
}

core::Result<size_t> encodeResult(const SendResult &result, core::Span<uint8_t> output) {
    if (!output.data() || output.size() < 12 || result.code > NBT_MOST_BUSY) {
        return encodeFailure();
    }
    uint8_t *out = output.data();
    memset(out, 0, 12);
    writePrefix(out, NBT_MOST_WIRE_RESULT);
    put32(out + 4, result.request);
    out[8] = result.code;
    return core::Result<size_t>::success(12);
}

} // namespace wire
} // namespace sdk
} // namespace nbt

int nbt_most_wire_type(const uint8_t *record, size_t length) {
    nbt::core::Result<nbt::sdk::wire::RecordType> res =
        nbt::sdk::wire::type(nbt::core::Span<const uint8_t>(record, record ? length : 0));
    return res.get() ? static_cast<int>(res.value()) : -1;
}

int nbt_most_wire_decode_state(const uint8_t *r, size_t length, nbt_most_wire_state *out) {
    return nbt::sdk::wire::unpack(
        nbt::sdk::wire::decodeState(nbt::core::Span<const uint8_t>(r, r ? length : 0)), out);
}

long nbt_most_wire_decode_registry(const uint8_t *r, size_t length, nbt_most_wire_entry *entries,
                                   size_t max) {
    if (max && !entries) {
        return -1;
    }
    nbt::core::Result<size_t> res =
        nbt::sdk::wire::decodeRegistry(nbt::core::Span<const uint8_t>(r, r ? length : 0),
                                       nbt::core::Span<nbt_most_wire_entry>(entries, max));
    return res.get() ? static_cast<long>(res.value()) : -1;
}

int nbt_most_wire_decode_message(const uint8_t *r, size_t length, nbt_most_wire_message *out) {
    return nbt::sdk::wire::unpack(
        nbt::sdk::wire::decodeMessage(nbt::core::Span<const uint8_t>(r, r ? length : 0)), out);
}

int nbt_most_wire_decode_result(const uint8_t *r, size_t length, nbt_most_wire_result *out) {
    return nbt::sdk::wire::unpack(
        nbt::sdk::wire::decodeResult(nbt::core::Span<const uint8_t>(r, r ? length : 0)), out);
}

long nbt_most_wire_encode_state(const nbt_most_wire_state *s, uint8_t *out, size_t size) {
    if (!s) {
        return -1;
    }
    nbt::core::Result<size_t> res =
        nbt::sdk::wire::encodeState(*s, nbt::core::Span<uint8_t>(out, out ? size : 0));
    return res.get() ? static_cast<long>(res.value()) : -1;
}

long nbt_most_wire_encode_registry(const nbt_most_wire_entry *e, size_t count, uint8_t *out,
                                   size_t size) {
    if (count && !e) {
        return -1;
    }
    nbt::core::Result<size_t> res =
        nbt::sdk::wire::encodeRegistry(nbt::core::Span<const nbt_most_wire_entry>(e, count),
                                       nbt::core::Span<uint8_t>(out, out ? size : 0));
    return res.get() ? static_cast<long>(res.value()) : -1;
}

long nbt_most_wire_encode_message(int type, const nbt_most_wire_message *m, uint8_t *out,
                                  size_t size) {
    if (!m) {
        return -1;
    }
    nbt::core::Result<size_t> res =
        nbt::sdk::wire::encodeMessage(type, *m, nbt::core::Span<uint8_t>(out, out ? size : 0));
    return res.get() ? static_cast<long>(res.value()) : -1;
}

long nbt_most_wire_encode_result(const nbt_most_wire_result *r, uint8_t *out, size_t size) {
    if (!r) {
        return -1;
    }
    nbt::core::Result<size_t> res =
        nbt::sdk::wire::encodeResult(*r, nbt::core::Span<uint8_t>(out, out ? size : 0));
    return res.get() ? static_cast<long>(res.value()) : -1;
}
