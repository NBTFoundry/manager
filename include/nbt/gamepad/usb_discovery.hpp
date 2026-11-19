#ifndef NBT_GAMEPAD_USB_DISCOVERY_HPP
#define NBT_GAMEPAD_USB_DISCOVERY_HPP
#ifdef __QNX__
#include "nbt/gamepad/usbdi.hpp"
namespace nbt {
namespace gamepad {
/* Read-only discovery before claiming the exact controller interface. */
bool discoverInterface(const usbd_device_instance_t &arrival, usbd_device_instance_t &selected);
} // namespace gamepad
} // namespace nbt
#endif
#endif
