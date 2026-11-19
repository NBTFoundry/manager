#ifndef NBT_GAMEPAD_USB_CONFIGURATION_HPP
#define NBT_GAMEPAD_USB_CONFIGURATION_HPP
#ifdef __QNX__
#include "nbt/gamepad/usbdi.hpp"
namespace nbt {
namespace gamepad {
bool configureUsb(usbd_device *device, uint8_t configuration, uint8_t interface, bool hid);
}
} // namespace nbt
#endif
#endif
