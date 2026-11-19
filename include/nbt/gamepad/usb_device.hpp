// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GAMEPAD_USB_DEVICE_HPP
#define NBT_GAMEPAD_USB_DEVICE_HPP
#ifdef __QNX__
#include "nbt/core/mutex.hpp"
#include "nbt/gamepad/reports.hpp"
#include "nbt/gamepad/usbdi.hpp"
namespace nbt {
namespace gamepad {
/* One attachment: drain/detach, free resources, then disconnect the stack. */
class UsbDevice {
public:
    UsbDevice();
    bool open(usbd_connection *connection, usbd_device_instance_t instance);
    int poll(uint8_t out[64], size_t &size);
    bool write(const uint8_t *data, size_t size);
    void cancel();
    void clear();
    Family family() const {
        return family_;
    }
    unsigned vendor() const {
        return vendor_;
    }
    unsigned product() const {
        return product_;
    }
    /* The step the last failed open stopped at (a fixed string), or NULL. */
    const char *failure() const {
        return failure_;
    }

private:
    static void completed(usbd_urb *, usbd_pipe *, void *self);
    static void writeCompleted(usbd_urb *, usbd_pipe *, void *self);
    bool submit();
    bool waitWrite(uint64_t timeoutMs);
    core::Mutex mutex_;
    usbd_device *device_;
    usbd_pipe *in_, *out_;
    usbd_urb *read_, *write_;
    uint8_t *input_, *output_;
    bool pending_, done_, writeDone_;
    Family family_;
    unsigned vendor_, product_, packetSize_;
    const char *failure_;
};
} // namespace gamepad
} // namespace nbt
#endif
#endif
