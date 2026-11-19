#include "nbt/gamepad/usb_discovery.hpp"
#ifdef __QNX__
#include "nbt/core/check.hpp"
#include "nbt/gamepad/reports.hpp"
#include "nbt/log.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
namespace nbt {
namespace gamepad {
namespace {
class DescriptorProbe {
public:
    DescriptorProbe() : connection(NULL), device(NULL) {
    }
    DescriptorProbe(const DescriptorProbe &) = delete;
    DescriptorProbe &operator=(const DescriptorProbe &) = delete;
    ~DescriptorProbe() {
        if (device) {
            const int result = usbdi().detach(device);
            NBT_CHECK(!result || result == ENODEV || result == ENOENT);
        }
        if (connection) {
            NBT_CHECK(usbdi().disconnect(connection) == 0);
        }
    }
    bool open(usbd_device_instance_t instance) {
        usbd_connect_parm_t parameters;
        memset(&parameters, 0, sizeof(parameters));
        parameters.path = NULL; // libusbdi's default, as NBT-AAD uses on the car
        parameters.vusb = USB_VERSION;
        parameters.vusbd = USBD_VERSION;
        parameters.connect_wait = 1;
        // No callbacks: descriptor-only shared access, never exclusive I/O.
        const int connected = usbdi().connect(&parameters, &connection);
        if (connected) {
            nbt_logf("gamepad: USB probe connect failed (%d)", connected);
            return false;
        }
        const int attached = usbdi().attach(connection, &instance, 0, &device);
        if (attached) {
            nbt_logf("gamepad: USB probe attach(%04x:%04x) failed (%d)",
                     static_cast<unsigned>(instance.ident.vendor),
                     static_cast<unsigned>(instance.ident.device), attached);
            return false;
        }
        return true;
    }
    usbd_connection *connection;
    usbd_device *device;
};
} // namespace
namespace {
bool select(const usbd_device_instance_t &arrival, unsigned config, unsigned iface,
            usbd_device_instance_t &selected) {
    selected = arrival;
    selected.config = static_cast<uint8_t>(config);
    selected.iface = static_cast<uint8_t>(iface);
    selected.alternate = 0;
    return true;
}
} // namespace
bool discoverInterface(const usbd_device_instance_t &arrival, usbd_device_instance_t &selected) {
    DescriptorProbe probe;
    if (!probe.open(arrival)) {
        return false;
    }
    usbd_device_descriptor_t *device = usbdi().device_descriptor(probe.device, NULL);
    if (!device || device->bLength < 18) {
        nbt_logf("gamepad: USB probe missing device descriptor");
        return false;
    }
    const unsigned vid = device->idVendor, pid = device->idProduct;
    // io-usb's own view, interface by interface (as UsbDevice::open reads it):
    // on the car the descriptor walk below found nothing for either pad
    // (2026-11-18), while io-usb reported every interface's class.
    unsigned seen = 0;
    for (unsigned config = 1; config <= 4; ++config) {
        for (unsigned iface = 0; iface < 16; ++iface) {
            usbd_interface_descriptor_t *d = usbdi().interface_descriptor(
                probe.device, static_cast<uint8_t>(config), static_cast<uint8_t>(iface), 0, NULL);
            if (!d || d->bLength < 9) {
                continue;
            }
            ++seen;
            nbt_logf("gamepad: USB %04x:%04x config %u interface %u class %02x/%02x/%02x", vid, pid,
                     config, iface, d->bInterfaceClass, d->bInterfaceSubClass,
                     d->bInterfaceProtocol);
            const Family family = identify(vid, pid, d->bInterfaceClass, d->bInterfaceSubClass,
                                           d->bInterfaceProtocol);
            if (family != Unsupported && (family != Gip || d->bInterfaceNumber == 0)) {
                return select(arrival, config, d->bInterfaceNumber, selected);
            }
        }
    }
    // The descriptor walk, kept as a fallback; says where it stops.
    for (int c = 0; c < 8; ++c) {
        usbd_desc_node *configNode = NULL;
        usbd_descriptors_t *config =
            usbdi().parse_descriptors(probe.device, NULL, USB_DESC_CONFIGURATION, c, &configNode);
        if (!config) {
            nbt_logf("gamepad: USB %04x:%04x descriptor walk: no configuration %d", vid, pid, c);
            break;
        }
        if (config->configuration.bLength < 9 || !config->configuration.bConfigurationValue ||
            config->configuration.bNumInterfaces > 16) {
            nbt_logf("gamepad: USB %04x:%04x invalid configuration descriptor", vid, pid);
            return false;
        }
        for (int i = 0; i < 32; ++i) {
            usbd_descriptors_t *item =
                usbdi().parse_descriptors(probe.device, configNode, USB_DESC_INTERFACE, i, NULL);
            if (!item) {
                nbt_logf("gamepad: USB %04x:%04x descriptor walk: configuration %u has %d "
                         "interfaces",
                         vid, pid, config->configuration.bConfigurationValue, i);
                break;
            }
            const usbd_interface_descriptor_t &iface = item->interface;
            if (iface.bLength < 9 || iface.bAlternateSetting) {
                continue;
            }
            const Family family = identify(vid, pid, iface.bInterfaceClass,
                                           iface.bInterfaceSubClass, iface.bInterfaceProtocol);
            if (family == Unsupported || (family == Gip && iface.bInterfaceNumber != 0)) {
                continue;
            }
            return select(arrival, config->configuration.bConfigurationValue,
                          iface.bInterfaceNumber, selected);
        }
    }
    nbt_logf("gamepad: USB %04x:%04x has no matching interface (%u read directly)", vid, pid, seen);
    return false;
}
} // namespace gamepad
} // namespace nbt
#endif
