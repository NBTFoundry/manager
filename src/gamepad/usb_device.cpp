// SPDX-License-Identifier: MPL-2.0
#include "nbt/gamepad/usb_device.hpp"
#include "nbt/gamepad/usb_configuration.hpp"
#include "nbt/gamepad/usb_discovery.hpp"
#include "nbt/log.h"
#include "nbt/util.h"
#ifdef __QNX__
#include <errno.h>
#include <poll.h>
#include <string.h>
namespace nbt {
namespace gamepad {
namespace {
void checkedClose(int result) {
    NBT_CHECK(!result || result == ENODEV || result == ENOENT);
}
} // namespace
UsbDevice::UsbDevice()
    : mutex_(), device_(NULL), in_(NULL), out_(NULL), read_(NULL), write_(NULL), input_(NULL),
      output_(NULL), pending_(false), done_(false), writeDone_(false), family_(Unsupported),
      vendor_(0), product_(0), packetSize_(0), failure_(NULL) {
}
bool UsbDevice::open(usbd_connection *connection, usbd_device_instance_t instance) {
    usbd_device_instance_t selected;
    failure_ = "no gamepad interface";
    if (!discoverInterface(instance, selected)) {
        return false;
    }
    instance = selected;
    const int attached = usbdi().attach(connection, &instance, 0, &device_);
    if (attached) {
        nbt_logf("gamepad: USB attach failed (%d)", attached);
        failure_ = "attach failed";
        return false;
    }
    auto fail = [this](const char *reason) {
        nbt_logf("gamepad: USB %04x:%04x %s", vendor_, product_, reason);
        cancel();
        clear();
        failure_ = reason;
        return false;
    };
    usbd_device_descriptor_t *dev = usbdi().device_descriptor(device_, NULL);
    usbd_desc_node *interfaceNode = NULL;
    usbd_interface_descriptor_t *iface = usbdi().interface_descriptor(
        device_, static_cast<uint8_t>(instance.config), static_cast<uint8_t>(instance.iface),
        static_cast<uint8_t>(instance.alternate), &interfaceNode);
    if (!dev || !iface || iface->bLength < 9 || iface->bNumEndpoints > 16) {
        return fail("invalid device/interface descriptor");
    }
    vendor_ = dev->idVendor;
    product_ = dev->idProduct;
    family_ = identify(vendor_, product_, iface->bInterfaceClass, iface->bInterfaceSubClass,
                       iface->bInterfaceProtocol);
    if (family_ == Unsupported) {
        return fail("unsupported interface");
    }
    const uint8_t config = static_cast<uint8_t>(instance.config);
    const uint8_t number = static_cast<uint8_t>(instance.iface);
    const uint8_t alternate = static_cast<uint8_t>(instance.alternate);
    if (!configureUsb(device_, config, number, family_ != Gip)) {
        return fail("configuration failed");
    }
    // Read again once configured: selecting the configuration can rebuild
    // io-usb's descriptors. On the car (dev122) the walk below gave one of
    // each pad's two endpoints: no OUT endpoint for the Xbox pad, no light
    // bar for the DualShock 4. Then io-usb's lookup by address fills a gap.
    iface = usbdi().interface_descriptor(device_, config, number, alternate, &interfaceNode);
    if (!iface || iface->bLength < 9) {
        return fail("invalid interface descriptor once configured");
    }
    bool openFailed = false;
    auto use = [this, &openFailed](const usbd_endpoint_descriptor_t *ep, const char *how) {
        if (!ep || ep->bLength < 7) {
            return;
        }
        nbt_logf("gamepad: USB %04x:%04x endpoint 0x%02x type %u size %u interval %u (%s)", vendor_,
                 product_, ep->bEndpointAddress, ep->bmAttributes & 3u,
                 static_cast<unsigned>(ep->wMaxPacketSize), ep->bInterval, how);
        usbd_pipe **pipe = ep->bEndpointAddress & 0x80 ? &in_ : &out_;
        if ((ep->bmAttributes & 3) != 3 || *pipe || !(ep->bEndpointAddress & 15) ||
            !ep->wMaxPacketSize || ep->wMaxPacketSize > 64 || !ep->bInterval) {
            return; // not an interrupt endpoint this pad can use, or one already open
        }
        usbd_descriptors_t *descriptor =
            reinterpret_cast<usbd_descriptors_t *>(const_cast<usbd_endpoint_descriptor_t *>(ep));
        if (usbdi().open_pipe(device_, descriptor, pipe)) {
            *pipe = NULL;
            openFailed = true;
            return;
        }
        if (pipe == &in_) {
            packetSize_ = ep->wMaxPacketSize;
        }
    };
    for (int i = 0; i < 16 && (!in_ || !out_); ++i) {
        usbd_descriptors_t *descriptor =
            usbdi().parse_descriptors(device_, interfaceNode, USB_DESC_ENDPOINT, i, NULL);
        if (!descriptor) {
            break;
        }
        use(&descriptor->endpoint, "walk");
    }
    for (unsigned n = 1; n < 16 && (!in_ || !out_); ++n) {
        if (!in_) {
            use(usbdi().endpoint_descriptor(device_, config, number, alternate,
                                            static_cast<uint8_t>(0x80 | n), NULL),
                "lookup");
        }
        if (!out_) {
            use(usbdi().endpoint_descriptor(device_, config, number, alternate,
                                            static_cast<uint8_t>(n), NULL),
                "lookup");
        }
    }
    if (openFailed && (!in_ || (family_ == Gip && !out_))) {
        return fail("open pipe failed");
    }
    if (!in_ || (family_ == Gip && !out_) || packetSize_ < (family_ == Gip ? 18u : 64u)) {
        return fail("missing interrupt endpoints");
    }
    nbt_logf("gamepad: USB %04x:%04x interface %u", vendor_, product_,
             static_cast<unsigned>(instance.iface));
    read_ = usbdi().alloc_urb(NULL);
    write_ = usbdi().alloc_urb(NULL);
    input_ = static_cast<uint8_t *>(usbdi().alloc(64));
    output_ = static_cast<uint8_t *>(usbdi().alloc(64));
    if (!read_ || !write_ || !input_ || !output_ || !submit()) {
        return fail("URB allocation or initial submit failed");
    }
    failure_ = NULL;
    return true;
}
void UsbDevice::completed(usbd_urb *, usbd_pipe *, void *opaque) {
    UsbDevice *self = static_cast<UsbDevice *>(opaque);
    core::LockGuard lock(self->mutex_);
    self->done_ = true;
}
void UsbDevice::writeCompleted(usbd_urb *, usbd_pipe *, void *opaque) {
    UsbDevice *self = static_cast<UsbDevice *>(opaque);
    core::LockGuard lock(self->mutex_);
    self->writeDone_ = true;
}
bool UsbDevice::submit() {
    {
        core::LockGuard lock(mutex_);
        done_ = false;
        pending_ = true;
    }
    if (usbdi().setup_interrupt(read_, URB_DIR_IN | URB_SHORT_XFER_OK, input_, packetSize_) ||
        usbdi().io(read_, in_, &completed, this, 250)) {
        core::LockGuard lock(mutex_);
        pending_ = false;
        return false;
    }
    return true;
}
int UsbDevice::poll(uint8_t out[64], size_t &size) {
    {
        core::LockGuard lock(mutex_);
        if (!done_) {
            return 0;
        }
        pending_ = false;
    }
    uint32_t status = 0, length = 0;
    if (usbdi().urb_status(read_, &status, &length)) {
        return -1;
    }
    int result = 0;
    if ((status & USBD_URB_STATUS_MASK) == USBD_STATUS_CMP && !(status & USBD_USB_STATUS_MASK)) {
        if (length > packetSize_) {
            return -1;
        }
        memcpy(out, input_, length);
        size = length;
        result = 1;
    } else if ((status & USBD_URB_STATUS_MASK) != USBD_STATUS_TIMEOUT) {
        return -1;
    }
    return submit() ? result : -1;
}
bool UsbDevice::waitWrite(uint64_t timeoutMs) {
    uint64_t now = 0;
    if (nbt_monotonic_ms(&now)) {
        return false;
    }
    const uint64_t deadline = now + timeoutMs;
    for (;;) {
        {
            core::LockGuard lock(mutex_);
            if (writeDone_) {
                return true;
            }
        }
        if (nbt_monotonic_ms(&now) || now >= deadline) {
            return false;
        }
        (void)::poll(NULL, 0, 2);
    }
}
bool UsbDevice::write(const uint8_t *data, size_t size) {
    if (!out_ || !data || !size || size > 64) {
        return false;
    }
    memcpy(output_, data, size);
    {
        core::LockGuard lock(mutex_);
        writeDone_ = false;
    }
    // QNX libusbdi runs non-control usbd_io asynchronously even with a NULL callback.
    if (usbdi().setup_interrupt(write_, URB_DIR_OUT, output_, static_cast<uint32_t>(size)) ||
        usbdi().io(write_, out_, &writeCompleted, this, 250)) {
        nbt_logf("gamepad: USB write submit failed");
        return false;
    }
    if (!waitWrite(500)) {
        (void)usbdi().abort_pipe(out_);
        (void)waitWrite(500);
        nbt_logf("gamepad: USB write timed out");
        return false;
    }
    uint32_t status = 0, length = 0;
    if (usbdi().urb_status(write_, &status, &length) ||
        (status & USBD_URB_STATUS_MASK) != USBD_STATUS_CMP || (status & USBD_USB_STATUS_MASK) ||
        length != size) {
        nbt_logf("gamepad: USB write failed (status=0x%08x len=%u/%u)", status, length,
                 static_cast<unsigned>(size));
        return false;
    }
    return true;
}
void UsbDevice::cancel() {
    if (in_) {
        checkedClose(usbdi().abort_pipe(in_));
    }
    if (out_) {
        checkedClose(usbdi().abort_pipe(out_));
    }
    if (device_) {
        uint64_t now = 0;
        NBT_CHECK(nbt_monotonic_ms(&now) == 0);
        const uint64_t deadline = now + 1000;
        int result;
        // detach refuses while callbacks still hold a device reference, and
        // closes its pipes once those callbacks have returned (SDK 6.5).
        while ((result = usbdi().detach(device_)) == EBUSY) {
            NBT_CHECK(nbt_monotonic_ms(&now) == 0 && now < deadline);
            (void)::poll(NULL, 0, 5);
        }
        checkedClose(result);
    }
    device_ = NULL;
    in_ = out_ = NULL;
}
void UsbDevice::clear() {
    if (read_) {
        usbdi().free_urb(read_);
    }
    if (write_) {
        usbdi().free_urb(write_);
    }
    if (input_) {
        usbdi().free(input_);
    }
    if (output_) {
        usbdi().free(output_);
    }
    read_ = write_ = NULL;
    input_ = output_ = NULL;
    pending_ = done_ = writeDone_ = false;
    family_ = Unsupported;
    vendor_ = product_ = packetSize_ = 0;
}
} // namespace gamepad
} // namespace nbt
#endif
