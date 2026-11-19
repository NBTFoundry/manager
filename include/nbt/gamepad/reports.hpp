#ifndef NBT_GAMEPAD_REPORTS_HPP
#define NBT_GAMEPAD_REPORTS_HPP
#include "nbt/gamepad_wire.h"
namespace nbt {
namespace gamepad {
enum Family { Unsupported, Gip, DualShock4, DualSense };
Family identify(unsigned vendor, unsigned product, unsigned cls, unsigned sub, unsigned protocol);
/* State is changed only after a fully validated report. Unknown reports return 0. */
int decodeSony(Family family, const uint8_t *data, size_t size, nbt_gamepad_wire_state &state);
/* DualShock 4 USB output report 0x05 (32 bytes) setting the light bar, motors
 * off, no flashing: until a host sends one the pad shows its charging colour.
 * Layout as Linux's hid-sony sends it. Returns the size. */
size_t dualShock4LightBar(uint8_t out[64], uint8_t red, uint8_t green, uint8_t blue);
class GipSession {
public:
    GipSession();
    void reset(unsigned vendor, unsigned product);
    int feed(const uint8_t *data, size_t size, nbt_gamepad_wire_state &state);
    /* Initialization and acknowledgments share this bounded output queue. */
    size_t output(uint8_t out[64]);

private:
    bool enqueue(uint8_t command, uint8_t options, uint8_t sequence, const uint8_t *payload,
                 size_t size);
    void initialize();
    uint8_t packets_[16][64], lengths_[16], head_, count_, sequence_;
    unsigned vendor_, product_;
};
} // namespace gamepad
} // namespace nbt
#endif
