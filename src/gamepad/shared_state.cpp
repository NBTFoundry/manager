#include "nbt/gamepad/shared_state.hpp"
#include "nbt/capture/control.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
namespace nbt {
namespace gamepad {
namespace {
const uint8_t kMagic[4] = {'N', 'G', 'P', 'S'};
void put(uint8_t *p, uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
}
uint64_t get(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) {
        v |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return v;
}
nbt_gamepad_wire_state disconnected() {
    nbt_gamepad_wire_state s;
    memset(&s, 0, sizeof(s));
    return s;
}
} // namespace
void encodeRecord(const nbt_gamepad_wire_state &state, uint32_t count, uint64_t now,
                  uint8_t out[kStateRecordSize]) {
    memset(out, 0, kStateRecordSize);
    memcpy(out, kMagic, sizeof(kMagic));
    put(out + 4, count, 4);
    put(out + 8, now, 8);
    if (nbt_gamepad_wire_encode(&state, out + 16, NBT_GAMEPAD_WIRE_SIZE) < 0) {
        memset(out, 0, kStateRecordSize); // an invalid state is published as none
        return;
    }
    put(out + 40, count, 4);
}
nbt_gamepad_wire_state decodeRecord(const uint8_t *p, size_t size, uint64_t now) {
    nbt_gamepad_wire_state s;
    if (!p || size != kStateRecordSize || memcmp(p, kMagic, sizeof(kMagic)) ||
        get(p + 4, 4) != get(p + 40, 4) ||
        nbt_gamepad_wire_decode(p + 16, NBT_GAMEPAD_WIRE_SIZE, &s)) {
        return disconnected();
    }
    const uint64_t stamp = get(p + 8, 8);
    if (now < stamp || now - stamp > kStateStaleMs) {
        return disconnected(); // the GUI stopped publishing
    }
    return s;
}
StateWriter::StateWriter() : fd_(), count_(0) {
}
bool StateWriter::open(const char *path) {
    fd_.reset(path ? nbt_capture_open_shmem_checked(path, 1) : -1);
    return fd_.valid();
}
void StateWriter::publish(const nbt_gamepad_wire_state &state, uint64_t now) {
    if (!fd_.valid()) {
        return;
    }
    uint8_t record[kStateRecordSize];
    encodeRecord(state, ++count_, now, record);
    ssize_t n;
    do {
        n = pwrite(fd_.get(), record, sizeof(record), 0);
    } while (n < 0 && errno == EINTR);
}
StateReader::StateReader() : fd_(), retryAt_(0) {
}
nbt_gamepad_wire_state StateReader::read(const char *path, uint64_t now) {
    if (!fd_.valid() && path && now >= retryAt_) {
        retryAt_ = now + 1000;
        fd_.reset(nbt_capture_open_shmem_checked(path, 0));
    }
    if (!fd_.valid()) {
        return disconnected();
    }
    uint8_t record[kStateRecordSize];
    ssize_t n;
    do {
        n = pread(fd_.get(), record, sizeof(record), 0);
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        fd_.reset(); // opened again within a second
        return disconnected();
    }
    return decodeRecord(record, static_cast<size_t>(n), now);
}
} // namespace gamepad
} // namespace nbt
