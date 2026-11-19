#ifndef NBT_GAMEPAD_USBDI_HPP
#define NBT_GAMEPAD_USBDI_HPP
#ifdef __QNX__
#include <sys/usbdi.h>
namespace nbt {
namespace gamepad {
#define NBT_USBDI_FUNCTIONS(X)                                                                     \
    X(connect)                                                                                     \
    X(disconnect)                                                                                  \
    X(attach)                                                                                      \
    X(detach)                                                                                      \
    X(device_descriptor)                                                                           \
    X(interface_descriptor)                                                                        \
    X(parse_descriptors)                                                                           \
    X(select_config)                                                                               \
    X(select_interface)                                                                            \
    X(open_pipe)                                                                                   \
    X(close_pipe)                                                                                  \
    X(abort_pipe)                                                                                  \
    X(alloc)                                                                                       \
    X(free)                                                                                        \
    X(alloc_urb)                                                                                   \
    X(free_urb)                                                                                    \
    X(setup_interrupt)                                                                             \
    X(setup_vendor)                                                                                \
    X(io)                                                                                          \
    X(urb_status)
/* The libusbdi entry points the gamepad uses, all from one library. */
struct Usbdi {
#define NBT_USBDI_MEMBER(name) decltype(&::usbd_##name) name;
    NBT_USBDI_FUNCTIONS(NBT_USBDI_MEMBER)
#undef NBT_USBDI_MEMBER
};
/* The car's /lib/libusbdi.so.2 (NBT-AAD's, proven on the car) when it loads
 * with every entry point; otherwise the SDK copy linked in. Chosen once and
 * logged; never mixed, since each library keeps its own pools. */
const Usbdi &usbdi();
} // namespace gamepad
} // namespace nbt
#endif
#endif
