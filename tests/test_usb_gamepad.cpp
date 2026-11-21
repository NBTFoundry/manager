// SPDX-License-Identifier: MPL-2.0
/* Compile the production QNX backend against this deterministic USB API shim. */
#include "nbt/gamepad/transport.hpp"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/usbdi.h>
static usbd_connection connection, probeConnection;
static usbd_device device, probeDevice;
static usbd_pipe pipes[2], controlPipe;
static usbd_desc_node node, configNode;
static usbd_device_instance_t instance;
static usbd_funcs_t *callbacks;
static bool live, probeLive;
static unsigned probeAttached;
static unsigned opened, allocated, attached;
static unsigned packetSize = 64;
static bool defer, xbox;
static uint16_t sonyProduct = 0x09cc;
static uint8_t configuration, alternate;
static bool stallGetInterface;
// The car (dev122): the walk under the interface gives only its first endpoint.
static bool shortWalk;
static unsigned lookups;
static unsigned selectedConfigs, selectedAlternates, idleRequests, writes;
static void (*pending)(usbd_urb *, usbd_pipe *, void *);
static usbd_urb *pendingUrb;
static usbd_pipe *pendingPipe;
static void *pendingContext;
int usbd_connect(usbd_connect_parm_t *p, usbd_connection **out) {
    if (!p->funcs) {
        assert(live && !probeLive);
        probeLive = true;
        *out = &probeConnection;
        return 0;
    }
    assert(!live && !probeLive && p->connect_wait == 1);
    assert(p->ident->vendor == UINT32_MAX);
    callbacks = p->funcs;
    *out = &connection;
    live = true;
    callbacks->insertion(&connection, &instance);
    return 0;
}
int usbd_disconnect(usbd_connection *conn) {
    if (conn == &probeConnection) {
        assert(probeLive && !probeAttached);
        probeLive = false;
        return 0;
    }
    assert(!opened && !attached && !allocated);
    live = false;
    return 0;
}
int usbd_attach(usbd_connection *conn, usbd_device_instance_t *i, size_t, usbd_device **out) {
    if (conn == &probeConnection) {
        ++probeAttached;
        *out = &probeDevice;
        return 0;
    }
    assert(i->config == 1 && i->iface == (xbox ? 0u : 3u) && i->alternate == 0);
    assert(i->generation == instance.generation);
    ++attached;
    *out = &device;
    return 0;
}
int usbd_detach(usbd_device *dev) {
    if (dev == &probeDevice) {
        assert(probeAttached == 1);
        --probeAttached;
        return 0;
    }
    assert(attached == 1 && live);
    if (pending) {
        pending(pendingUrb, pendingPipe, pendingContext);
        pending = NULL;
        return EBUSY;
    }
    opened = 0;
    --attached;
    return 0;
}
usbd_device_descriptor_t *usbd_device_descriptor(usbd_device *, usbd_desc_node **) {
    static usbd_device_descriptor_t d = {18, 1, 0x054c, 0x09cc};
    d.idVendor = static_cast<uint16_t>(xbox ? 0x045e : 0x054c);
    d.idProduct = xbox ? 0x02d1 : sonyProduct;
    return &d;
}
usbd_interface_descriptor_t *usbd_interface_descriptor(usbd_device *, uint8_t config, uint8_t iface,
                                                       uint8_t alt, usbd_desc_node **out) {
    // The pads' topology, as io-usb reported it on the car: DualShock 4 audio
    // 0-2 and HID 3; Xbox One GIP 0-2. Discovery asks for every number.
    if (config != 1 || alt != 0 || iface >= (xbox ? 3 : 4)) {
        return NULL;
    }
    static usbd_interface_descriptor_t d = {9, 4, 3, 0, 2, 3, 0, 0, 0};
    d.bInterfaceNumber = iface;
    d.bInterfaceClass = static_cast<uint8_t>(xbox ? 0xff : iface == 3 ? 3 : 1);
    d.bInterfaceSubClass = static_cast<uint8_t>(xbox ? 0x47 : 0);
    d.bInterfaceProtocol = static_cast<uint8_t>(xbox ? 0xd0 : 0);
    if (out) {
        *out = &node;
    }
    return &d;
}
usbd_descriptors_t *usbd_parse_descriptors(usbd_device *, usbd_desc_node *root, uint8_t type, int i,
                                           usbd_desc_node **location) {
    static usbd_descriptors_t config, interfaces[4];
    if (type == USB_DESC_CONFIGURATION) {
        assert(!root);
        if (i) {
            return NULL;
        }
        config.configuration.bLength = 9;
        config.configuration.bNumInterfaces = 4;
        config.configuration.bConfigurationValue = 1;
        if (location) {
            *location = &configNode;
        }
        return &config;
    }
    if (type == USB_DESC_INTERFACE) {
        assert(root == &configNode);
        if (i >= 4) {
            return NULL;
        }
        interfaces[i].interface.bLength = 9;
        interfaces[i].interface.bInterfaceClass = static_cast<uint8_t>(xbox     ? 0xff
                                                                       : i == 3 ? 3
                                                                                : 1);
        interfaces[i].interface.bInterfaceSubClass = static_cast<uint8_t>(xbox ? 0x47 : 0);
        interfaces[i].interface.bInterfaceProtocol = static_cast<uint8_t>(xbox ? 0xd0 : 0);
        interfaces[i].interface.bInterfaceNumber = static_cast<uint8_t>(i);
        return &interfaces[i];
    }
    assert(root == &node && type == USB_DESC_ENDPOINT && i >= 0);
    if (i >= (shortWalk ? 1 : 2)) {
        return NULL;
    }
    static usbd_descriptors_t d[2];
    d[i].endpoint.bLength = 7;
    d[i].endpoint.bDescriptorType = USB_DESC_ENDPOINT;
    d[i].endpoint.bEndpointAddress =
        static_cast<uint8_t>(xbox ? (i ? 0x01 : 0x81) : (i ? 0x03 : 0x84));
    d[i].endpoint.bmAttributes = 3;
    d[i].endpoint.wMaxPacketSize = static_cast<uint16_t>(packetSize);
    d[i].endpoint.bInterval =
        static_cast<uint8_t>(xbox                                               ? 4
                             : (sonyProduct == 0x0ce6 || sonyProduct == 0x0df2) ? 6
                                                                                : 5);
    return &d[i];
}
usbd_endpoint_descriptor_t *usbd_endpoint_descriptor(usbd_device *dev, uint8_t config,
                                                     uint8_t iface, uint8_t alt, uint8_t address,
                                                     usbd_desc_node **) {
    ++lookups;
    for (int i = 0; i < 2; ++i) {
        const bool was = shortWalk;
        shortWalk = false;
        usbd_descriptors_t *d = usbd_parse_descriptors(dev, &node, USB_DESC_ENDPOINT, i, NULL);
        shortWalk = was;
        if (config == 1 && iface == (xbox ? 0 : 3) && alt == 0 &&
            d->endpoint.bEndpointAddress == address) {
            return &d->endpoint;
        }
    }
    return NULL;
}
int usbd_open_pipe(usbd_device *, usbd_descriptors_t *ep, usbd_pipe **out) {
    if (ep->device.bDescriptorType == 1) {
        *out = &controlPipe;
        ++opened;
        return 0;
    }
    assert(ep->endpoint.bEndpointAddress == (xbox ? 0x81 : 0x84) ||
           ep->endpoint.bEndpointAddress == (xbox ? 0x01 : 0x03));
    *out = &pipes[ep->endpoint.bEndpointAddress & 0x80 ? 0 : 1];
    ++opened;
    return 0;
}
int usbd_abort_pipe(usbd_pipe *) {
    return 0;
}
int usbd_close_pipe(usbd_pipe *) {
    assert(opened);
    --opened;
    return 0;
}
usbd_urb *usbd_alloc_urb(usbd_urb *) {
    ++allocated;
    return static_cast<usbd_urb *>(calloc(1, sizeof(usbd_urb)));
}
void usbd_free_urb(usbd_urb *u) {
    assert(live);
    --allocated;
    free(u);
}
void *usbd_alloc(size_t n) {
    assert(n == 64 || n == 1);
    ++allocated;
    return calloc(1, n);
}
void usbd_free(void *p) {
    assert(live);
    --allocated;
    free(p);
}
int usbd_setup_interrupt(usbd_urb *u, uint32_t, void *p, uint32_t n) {
    u->data = p;
    u->length = n;
    return 0;
}
int usbd_setup_vendor(usbd_urb *u, uint32_t, uint16_t request, uint16_t type, uint16_t value,
                      uint16_t index, void *data, uint32_t length) {
    assert(value == 0 && length <= 1);
    u->data = data;
    u->length = length;
    u->request = request;
    u->type = type;
    u->index = index;
    return 0;
}
int usbd_select_config(usbd_device *, uint8_t value) {
    assert(configuration == 0 && value == 1);
    configuration = value;
    ++selectedConfigs;
    return 0;
}
int usbd_select_interface(usbd_device *, uint8_t iface, uint8_t value) {
    assert(iface == (xbox ? 0 : 3) && value == 0 && alternate != 0);
    alternate = value;
    ++selectedAlternates;
    return 0;
}
int usbd_io(usbd_urb *u, usbd_pipe *p, void (*cb)(usbd_urb *, usbd_pipe *, void *), void *ctx,
            uint32_t timeout) {
    // Control and output transfers time out; a pad's input read waits for a report.
    assert(live && timeout == (p == &controlPipe || p == &pipes[1] ? 250 : USBD_TIME_INFINITY));
    u->status = USBD_STATUS_CMP;
    if (p == &controlPipe) {
        assert(!cb && allocated >= 2);
        uint8_t *value = static_cast<uint8_t *>(u->data);
        if (u->type == (USB_DIRECTION_HOST | USB_TYPE_STANDARD | USB_RECIPIENT_DEVICE) &&
            u->request == USB_GET_CONFIGURATION) {
            *value = configuration;
        } else if (u->type == (USB_DIRECTION_HOST | USB_TYPE_STANDARD | USB_RECIPIENT_INTERFACE) &&
                   u->request == USB_GET_INTERFACE) {
            if (stallGetInterface) {
                u->status = 0x02000004; // USB 2.0 9.4.4 permits STALL on single-alternate
            } else {
                *value = alternate;
            }
        } else {
            assert(u->type == (USB_TYPE_CLASS | USB_RECIPIENT_INTERFACE) && u->request == 0x0a);
            ++idleRequests;
            u->status = 0x02000004; // SET_IDLE stall is nonfatal
        }
        return 0;
    }
    if (p == &pipes[1]) {
        if (!cb) {
            // Non-control usbd_io without callback does not complete synchronously on QNX.
            u->status = 0;
            return 0;
        }
        ++writes;
        cb(u, p, ctx);
        return 0;
    }
    if (cb) {
        uint8_t *data = static_cast<uint8_t *>(u->data);
        memset(data, 0, u->length);
        data[0] = 1;
        data[1] = data[2] = data[3] = data[4] = 128;
        data[5] = 8;
        if (defer) {
            pending = cb;
            pendingUrb = u;
            pendingPipe = p;
            pendingContext = ctx;
        } else {
            cb(u, p, ctx);
        }
    }
    return 0;
}
int usbd_urb_status(usbd_urb *u, uint32_t *status, uint32_t *length) {
    *status = u->status;
    *length = u->length;
    return 0;
}
int main() {
    instance.config = 1;
    instance.iface = 3;
    instance.generation = 1;
    instance.ident.vendor = 0x054c;
    instance.ident.device = 0x09cc;
    instance.ident.dclass = 0; // device-level arrival of a composite Sony controller
    std::unique_ptr<nbt::gamepad::UsbTransport> usb = nbt::gamepad::makeUsbTransport();
    assert(usb && usb->start());
    uint8_t data[64];
    size_t size = 0;
    assert(usb->poll(data, size) == 1 && size == 64 && usb->family() == nbt::gamepad::DualShock4);
    assert(selectedConfigs == 1 && !selectedAlternates && idleRequests == 1);
    callbacks->removal(&connection, &instance);
    assert(usb->poll(data, size) == -1);
    usb->stop();
    assert(!live && !opened && !allocated && !attached);
    packetSize = 65;
    ++instance.generation;
    assert(usb->start() && usb->poll(data, size) == -1);
    usb->stop();
    assert(!allocated && !opened && !attached);
    // A rejected attachment is ignored until a new device generation arrives.
    assert(usb->start() && usb->poll(data, size) == 0);
    usb->stop();
    ++instance.generation;
    packetSize = 64;
    assert(usb->start() && usb->poll(data, size) == 1);
    usb->stop();
    assert(!allocated && !opened && !attached);
    // Completion may arrive while detach is draining pending requests.
    defer = true;
    ++instance.generation;
    assert(usb->start() && usb->poll(data, size) == 0);
    assert(pending && allocated);
    usb->stop();
    assert(!pending && !allocated && !opened && !attached);
    defer = false;
    xbox = true;
    stallGetInterface = true;
    ++instance.generation;
    instance.ident.vendor = 0x045e;
    instance.ident.device = 0x02d1;
    instance.ident.dclass = 0xff;
    instance.ident.subclass = 0x47;
    instance.ident.protocol = 0xd0;
    instance.iface = UINT32_MAX;
    lookups = 0;
    assert(usb->start() && usb->poll(data, size) == 1 && usb->family() == nbt::gamepad::Gip);
    const uint8_t power[] = {5, 0x20, 0, 1, 0};
    assert(usb->write(power, sizeof(power)) && writes == 1);
    usb->stop();
    assert(!lookups); // the walk gave both endpoints
    // As on the car: the walk stops after the IN endpoint, the lookup by
    // address finds the OUT one, and the pad opens with both.
    shortWalk = true;
    ++instance.generation;
    assert(usb->start() && usb->poll(data, size) == 1 && usb->family() == nbt::gamepad::Gip);
    assert(usb->write(power, sizeof(power)) && writes == 2 && lookups);
    usb->stop();
    shortWalk = false;
    stallGetInterface = false;
    xbox = false;
    sonyProduct = 0x0ce6;
    ++instance.generation;
    instance.ident.vendor = 0x054c;
    instance.ident.device = sonyProduct;
    instance.ident.dclass = instance.ident.subclass = instance.ident.protocol = 0;
    assert(usb->start() && usb->poll(data, size) == 1 && usb->family() == nbt::gamepad::DualSense);
    usb->stop();
    sonyProduct = 0x0df2;
    ++instance.generation;
    instance.ident.device = sonyProduct;
    assert(usb->start() && usb->poll(data, size) == 1 && usb->family() == nbt::gamepad::DualSense);
    usb->stop();
    assert(!allocated && !opened && !attached && !probeAttached);
    const unsigned selections = selectedConfigs;
    ++instance.generation;
    configuration = 2;
    assert(usb->start() && usb->poll(data, size) == -1);
    usb->stop();
    assert(configuration == 2 && selectedConfigs == selections); // preserve another configuration
    ++instance.generation;
    configuration = 1;
    alternate = 1;
    assert(usb->start() && usb->poll(data, size) == 1);
    usb->stop();
    assert(!alternate && selectedAlternates == 1 && selectedConfigs == selections);
    puts("QNX USB transport shim: OK");
}
