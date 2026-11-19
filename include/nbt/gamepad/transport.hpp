// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GAMEPAD_TRANSPORT_HPP
#define NBT_GAMEPAD_TRANSPORT_HPP
#include "nbt/gamepad/reports.hpp"
#include <memory>
namespace nbt {
namespace gamepad {
/* Why no pad is open, for the manager's Status page: usbd_connect's last
 * failure (0: none) and a pad that failed to open, with the step. */
struct UsbProblem {
    int connectError;
    unsigned vendor, product;
    const char *step; // a fixed string; NULL: no pad failed
};
/* All calls run on the service thread. Test transports use the same interface. */
class UsbTransport {
public:
    virtual ~UsbTransport() {
    }
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual int poll(uint8_t data[64], size_t &size) = 0;
    virtual bool write(const uint8_t *data, size_t size) = 0;
    virtual Family family() const = 0;
    virtual unsigned vendor() const = 0;
    virtual unsigned product() const = 0;
    virtual UsbProblem problem() const {
        const UsbProblem none = {0, 0, 0, NULL};
        return none;
    }
};
std::unique_ptr<UsbTransport> makeUsbTransport();
} // namespace gamepad
} // namespace nbt
#endif
