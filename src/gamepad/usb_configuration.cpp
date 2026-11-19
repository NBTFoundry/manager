#include "nbt/gamepad/usb_configuration.hpp"
#ifdef __QNX__
#include "nbt/core/check.hpp"
#include "nbt/log.h"
#include <cstddef>
namespace nbt {
namespace gamepad {
namespace {
class ControlTransfer {
public:
    ControlTransfer() : pipe_(NULL), urb_(NULL), byte_(NULL), status_(0) {
    }
    ControlTransfer(const ControlTransfer &) = delete;
    ControlTransfer &operator=(const ControlTransfer &) = delete;
    ~ControlTransfer() {
        if (pipe_) {
            NBT_CHECK(usbdi().close_pipe(pipe_) == 0);
        }
        if (urb_) {
            usbdi().free_urb(urb_);
        }
        if (byte_) {
            usbdi().free(byte_);
        }
    }
    bool open(usbd_device *device) {
        usbd_device_descriptor_t *descriptor = usbdi().device_descriptor(device, NULL);
        if (!descriptor ||
            usbdi().open_pipe(device, reinterpret_cast<usbd_descriptors_t *>(descriptor), &pipe_)) {
            return false;
        }
        urb_ = usbdi().alloc_urb(NULL);
        byte_ = static_cast<uint8_t *>(usbdi().alloc(1));
        return urb_ && byte_;
    }
    bool request(uint16_t request, uint16_t type, uint16_t index, bool input) {
        status_ = 0;
        const uint32_t length = input ? 1u : 0u;
        const uint16_t direction = input ? USB_DIRECTION_HOST : USB_DIRECTION_DEVICE;
        if (usbdi().setup_vendor(urb_, input ? URB_DIR_IN : URB_DIR_OUT, request,
                                 static_cast<uint16_t>(type | direction), 0, index, byte_,
                                 length) ||
            usbdi().io(urb_, pipe_, NULL, NULL, 250)) {
            return false;
        }
        uint32_t transferred = 0;
        return !usbdi().urb_status(urb_, &status_, &transferred) &&
               (status_ & USBD_URB_STATUS_MASK) == USBD_STATUS_CMP &&
               !(status_ & USBD_USB_STATUS_MASK) && transferred == length;
    }
    uint8_t value() const {
        return *byte_;
    }
    uint32_t status() const {
        return status_;
    }

private:
    usbd_pipe *pipe_;
    usbd_urb *urb_;
    uint8_t *byte_;
    uint32_t status_;
};
} // namespace
bool configureUsb(usbd_device *device, uint8_t configuration, uint8_t interface, bool hid) {
    ControlTransfer control;
    if (!control.open(device)) {
        nbt_logf("gamepad: USB control pipe open failed");
        return false;
    }
    if (!control.request(USB_GET_CONFIGURATION,
                         USB_DIRECTION_HOST | USB_TYPE_STANDARD | USB_RECIPIENT_DEVICE, 0, true)) {
        nbt_logf("gamepad: USB GET_CONFIGURATION failed (status=0x%08x)", control.status());
        return false;
    }
    const uint8_t current = control.value();
    if (current != configuration) {
        if (current != 0) {
            nbt_logf("gamepad: USB unexpected active config %u (expected %u)", current,
                     configuration);
            return false;
        }
        const int selected = usbdi().select_config(device, configuration);
        if (selected) {
            nbt_logf("gamepad: USB select config %u failed (%d)", configuration, selected);
            return false;
        }
    }
    // USB 2.0 9.4.4 allows single-alternate interfaces to STALL GET_INTERFACE.
    if (control.request(USB_GET_INTERFACE,
                        USB_DIRECTION_HOST | USB_TYPE_STANDARD | USB_RECIPIENT_INTERFACE, interface,
                        true) &&
        control.value()) {
        const int selected = usbdi().select_interface(device, interface, 0);
        if (selected) {
            nbt_logf("gamepad: USB select interface %u failed (%d)", interface, selected);
            return false;
        }
    }
    if (hid) {
        // SET_IDLE report 0/duration 0 is best effort, as in Linux usbhid.
        (void)control.request(0x0a, USB_TYPE_CLASS | USB_RECIPIENT_INTERFACE, interface, false);
    }
    return true;
}
} // namespace gamepad
} // namespace nbt
#endif
