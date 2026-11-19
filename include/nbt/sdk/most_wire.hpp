#ifndef NBT_SDK_MOST_WIRE_HPP
#define NBT_SDK_MOST_WIRE_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/most_wire.h"

namespace nbt {
namespace sdk {
namespace wire {

enum RecordType {
    RecordState = NBT_MOST_WIRE_STATE,
    RecordRegistry = NBT_MOST_WIRE_REGISTRY,
    RecordMessage = NBT_MOST_WIRE_MESSAGE,
    RecordResult = NBT_MOST_WIRE_RESULT,
    RecordSend = NBT_MOST_WIRE_SEND
};

typedef nbt_most_wire_state State;
typedef nbt_most_wire_entry Entry;
typedef nbt_most_wire_message Message;
typedef nbt_most_wire_result SendResult;

core::Result<RecordType> type(core::Span<const uint8_t> record);
core::Result<State> decodeState(core::Span<const uint8_t> record);
core::Result<size_t> decodeRegistry(core::Span<const uint8_t> record, core::Span<Entry> entries);
core::Result<Message> decodeMessage(core::Span<const uint8_t> record);
core::Result<SendResult> decodeResult(core::Span<const uint8_t> record);
core::Result<size_t> encodeState(const State &state, core::Span<uint8_t> output);
core::Result<size_t> encodeRegistry(core::Span<const Entry> entries, core::Span<uint8_t> output);
core::Result<size_t> encodeMessage(int recordType, const Message &message,
                                   core::Span<uint8_t> output);
core::Result<size_t> encodeResult(const SendResult &result, core::Span<uint8_t> output);

} // namespace wire
} // namespace sdk
} // namespace nbt

#endif
