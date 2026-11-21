// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FAKE_USBDI_H
#define NBT_FAKE_USBDI_H
#include <stddef.h>
#include <stdint.h>
#define USB_VERSION 0x200
#define USBD_VERSION 0x101
#define _USBDI_NFUNCS 3
#define USB_DESC_ENDPOINT 5
#define USB_DESC_CONFIGURATION 2
#define USB_DESC_INTERFACE 4
#define USB_GET_CONFIGURATION 8
#define USB_GET_INTERFACE 10
#define USB_TYPE_STANDARD 0
#define USB_TYPE_CLASS 0x20
#define USB_RECIPIENT_DEVICE 0
#define USB_RECIPIENT_INTERFACE 1
#define USB_DIRECTION_DEVICE 0
#define USB_DIRECTION_HOST 0x80u
#define URB_DIR_IN 1
#define URB_DIR_OUT 2
#define URB_SHORT_XFER_OK 4
#define USBD_URB_STATUS_MASK 0xff000000u
#define USBD_USB_STATUS_MASK 0x00ffffffu
#define USBD_STATUS_CMP 0x01000000u
#define USBD_STATUS_TIMEOUT 0x04000000u
struct usbd_connection {
    int unused;
};
struct usbd_device {
    int unused;
};
struct usbd_pipe {
    int unused;
};
struct usbd_desc_node {
    int unused;
};
struct usbd_urb {
    void *data;
    uint32_t length, status;
    uint16_t request, type, index;
};
struct usbd_device_descriptor_t {
    uint8_t bLength, bDescriptorType;
    uint16_t idVendor, idProduct;
};
struct usbd_interface_descriptor_t {
    uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints,
        bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
};
struct usbd_endpoint_descriptor_t {
    uint8_t bLength, bDescriptorType, bEndpointAddress, bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
};
struct usbd_configuration_descriptor_t {
    uint8_t bLength, bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces, bConfigurationValue;
};
union usbd_descriptors_t {
    usbd_device_descriptor_t device;
    usbd_configuration_descriptor_t configuration;
    usbd_interface_descriptor_t interface;
    usbd_endpoint_descriptor_t endpoint;
};
struct usbd_device_ident_t {
    uint32_t vendor, device, dclass, subclass, protocol;
};
struct usbd_device_instance_t {
    uint8_t path, devno;
    uint16_t generation;
    usbd_device_ident_t ident;
    uint32_t config, iface, alternate;
};
struct usbd_funcs_t {
    uint32_t nentries;
    void (*insertion)(usbd_connection *, usbd_device_instance_t *);
    void (*removal)(usbd_connection *, usbd_device_instance_t *);
    void (*event)(usbd_connection *, usbd_device_instance_t *, uint16_t);
};
struct usbd_connect_parm_t {
    const char *path;
    uint16_t vusb, vusbd;
    usbd_device_ident_t *ident;
    usbd_funcs_t *funcs;
    uint16_t connect_wait;
};
int usbd_connect(usbd_connect_parm_t *, usbd_connection **);
int usbd_disconnect(usbd_connection *);
int usbd_attach(usbd_connection *, usbd_device_instance_t *, size_t, usbd_device **);
int usbd_detach(usbd_device *);
usbd_device_descriptor_t *usbd_device_descriptor(usbd_device *, usbd_desc_node **);
usbd_interface_descriptor_t *usbd_interface_descriptor(usbd_device *, uint8_t, uint8_t, uint8_t,
                                                       usbd_desc_node **);
usbd_descriptors_t *usbd_parse_descriptors(usbd_device *, usbd_desc_node *, uint8_t, int,
                                           usbd_desc_node **);
usbd_endpoint_descriptor_t *usbd_endpoint_descriptor(usbd_device *, uint8_t, uint8_t, uint8_t,
                                                     uint8_t, usbd_desc_node **);
int usbd_open_pipe(usbd_device *, usbd_descriptors_t *, usbd_pipe **);
int usbd_abort_pipe(usbd_pipe *);
int usbd_close_pipe(usbd_pipe *);
usbd_urb *usbd_alloc_urb(usbd_urb *);
void usbd_free_urb(usbd_urb *);
void *usbd_alloc(size_t);
void usbd_free(void *);
int usbd_setup_interrupt(usbd_urb *, uint32_t, void *, uint32_t);
int usbd_io(usbd_urb *, usbd_pipe *, void (*)(usbd_urb *, usbd_pipe *, void *), void *, uint32_t);
int usbd_setup_vendor(usbd_urb *, uint32_t, uint16_t, uint16_t, uint16_t, uint16_t, void *,
                      uint32_t);
int usbd_select_config(usbd_device *, uint8_t);
int usbd_select_interface(usbd_device *, uint8_t, uint8_t);
int usbd_urb_status(usbd_urb *, uint32_t *, uint32_t *);
#endif
