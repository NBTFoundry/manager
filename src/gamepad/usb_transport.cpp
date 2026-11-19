// SPDX-License-Identifier: MPL-2.0
#include "nbt/gamepad/transport.hpp"
#include <new>
#ifdef __QNX__
#include "nbt/gamepad/usb_device.hpp"
#include "nbt/gamepad/usb_discovery.hpp"
#include "nbt/log.h"
#include "nbt/util.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
namespace nbt {
namespace gamepad {
namespace {
class QnxUsb;
QnxUsb *callbackOwner = NULL; // initialized before connect, cleared after disconnect
/* A pad that failed to open is retried after 2 s, then 4, 8 ... up to 60 s: on
 * the car a pad failed GET_CONFIGURATION right after a hand-over between the GUI
 * and an application and worked when opened again (2026-11-18). */
const uint64_t kRetryFirstMs = 2000, kRetryMaxMs = 60000;
struct Blocked {
    usbd_device_instance_t instance;
    uint64_t retryAt, delay;
    bool seen;        // reported since the last connect; removals are missed while disconnected
    const char *step; // where the last open stopped (UsbDevice::failure)
};
class QnxUsb : public UsbTransport {
public:
    QnxUsb()
        : mutex_(), connection_(NULL), device_(), candidates_(), active_(), count_(0),
          attached_(false), removed_(false), blocked_(), blockedCount_(0), lastError_(0),
          connectedAt_(0) {
    }
    ~QnxUsb() {
        stop();
    }
    bool start() override {
        if (connection_ || callbackOwner) {
            return false;
        }
        usbd_connect_parm_t p;
        memset(&p, 0, sizeof(p));
        memset(&ids_, 0xff, sizeof(ids_));
        funcs_.nentries = _USBDI_NFUNCS;
        funcs_.insertion = &insert;
        funcs_.removal = &remove;
        funcs_.event = NULL;
        p.path = NULL; // libusbdi's default, as NBT-AAD uses on the car
        p.vusb = USB_VERSION;
        p.vusbd = USBD_VERSION;
        p.ident = &ids_;
        p.funcs = &funcs_;
        p.connect_wait = 1;
        {
            core::LockGuard lock(mutex_);
            for (unsigned n = 0; n < blockedCount_; ++n) {
                blocked_[n].seen = false;
            }
        }
        callbackOwner = this;
        const int connected = usbdi().connect(&p, &connection_);
        if (connected) {
            if (connected != lastError_) { // retried every 500 ms: log each new cause once
                nbt_logf("gamepad: usbd_connect failed (%d)", connected);
                lastError_ = connected;
            }
            connection_ = NULL;
            callbackOwner = NULL;
            return false;
        }
        lastError_ = 0;
        (void)nbt_monotonic_ms(&connectedAt_);
        return true;
    }
    void stop() override {
        device_.cancel();
        device_.clear(); // libusbdi destroys its allocator on the last disconnect
        if (connection_) {
            NBT_CHECK(usbdi().disconnect(connection_) == 0);
            connection_ = NULL;
        }
        if (callbackOwner == this) {
            callbackOwner = NULL;
        }
        core::LockGuard lock(mutex_);
        count_ = 0;
        attached_ = removed_ = false;
    }
    int poll(uint8_t out[64], size_t &size) override {
        if (!connection_) {
            return -1;
        }
        uint64_t now = 0;
        (void)nbt_monotonic_ms(&now);
        {
            core::LockGuard lock(mutex_);
            if (removed_) {
                return -1;
            }
            if (!attached_ && count_) {
                active_ = candidates_[--count_];
                attached_ = true;
            } else if (!attached_ && !retryDue(now)) {
                return 0;
            }
        }
        if (device_.family() == Unsupported) {
            const bool opened = device_.open(connection_, active_);
            core::LockGuard lock(mutex_);
            Blocked *b = blockedEntry(active_);
            if (opened) {
                if (b) {
                    *b = blocked_[--blockedCount_];
                }
            } else {
                block(b, now);
                return -1;
            }
        }
        return device_.poll(out, size);
    }
    bool write(const uint8_t *p, size_t n) override {
        return device_.write(p, n);
    }
    Family family() const override {
        return device_.family();
    }
    unsigned vendor() const override {
        return device_.vendor();
    }
    unsigned product() const override {
        return device_.product();
    }
    UsbProblem problem() const override {
        core::LockGuard lock(mutex_);
        UsbProblem p = {lastError_, 0, 0, NULL};
        if (blockedCount_) { // a pad still plugged in that could not be opened
            const Blocked &b = blocked_[blockedCount_ - 1];
            p.vendor = b.instance.ident.vendor;
            p.product = b.instance.ident.device;
            p.step = b.step;
        }
        return p;
    }

private:
    /* io-usb reuses a port's device number (and generation 0) for the next
     * device plugged there: the vendor and product tell them apart. On the car
     * an Xbox pad was taken for a PS4 pad that had failed there (2026-11-18). */
    static bool same(const usbd_device_instance_t &a, const usbd_device_instance_t &b) {
        return a.path == b.path && a.devno == b.devno && a.generation == b.generation &&
               a.ident.vendor == b.ident.vendor && a.ident.device == b.ident.device;
    }
    Blocked *blockedEntry(const usbd_device_instance_t &i) {
        for (unsigned n = 0; n < blockedCount_; ++n) {
            if (same(blocked_[n].instance, i)) {
                return &blocked_[n];
            }
        }
        return NULL;
    }
    /* Called with the lock held after a failed open. */
    void block(Blocked *b, uint64_t now) {
        if (!b) {
            if (blockedCount_ == 8) {
                memmove(blocked_, blocked_ + 1, 7 * sizeof(blocked_[0]));
                --blockedCount_;
            }
            b = &blocked_[blockedCount_++];
            b->instance = active_;
            b->delay = kRetryFirstMs;
        } else {
            b->delay = b->delay * 2 > kRetryMaxMs ? kRetryMaxMs : b->delay * 2;
        }
        b->seen = true;
        b->retryAt = now + b->delay;
        b->step = device_.failure() ? device_.failure() : "open failed";
        nbt_logf("gamepad: USB open %04x:%04x (dev=%u gen=%u) failed; next try in %u s",
                 static_cast<unsigned>(active_.ident.vendor),
                 static_cast<unsigned>(active_.ident.device), static_cast<unsigned>(active_.devno),
                 static_cast<unsigned>(active_.generation), static_cast<unsigned>(b->delay / 1000));
    }
    /* Called with the lock held while nothing is attached: forgets pads not
     * reported since connecting (unplugged meanwhile) and picks one whose
     * retry is due as the active device. */
    bool retryDue(uint64_t now) {
        for (unsigned n = 0; n < blockedCount_;) {
            if (!blocked_[n].seen && now >= connectedAt_ + 1000) {
                blocked_[n] = blocked_[--blockedCount_];
            } else {
                ++n;
            }
        }
        for (unsigned n = 0; n < blockedCount_; ++n) {
            if (blocked_[n].seen && now >= blocked_[n].retryAt) {
                active_ = blocked_[n].instance;
                attached_ = true;
                return true;
            }
        }
        return false;
    }
    static bool compositeGipCandidate(const usbd_device_ident_t &ident) {
        if (ident.dclass != 0 && ident.dclass != 0xef) {
            return false;
        }
        return ident.vendor == 0x045e || ident.vendor == 0x0e6f || ident.vendor == 0x0f0d ||
               ident.vendor == 0x24c6 || ident.vendor == 0x20d6 || ident.vendor == 0x2dc8;
    }
    static void insert(usbd_connection *, usbd_device_instance_t *i) {
        QnxUsb *s = callbackOwner;
        if (i) { // every device io-usb reports, gamepad or not: the car's evidence
            nbt_logf("gamepad: USB device %04x:%04x class %02x/%02x/%02x",
                     static_cast<unsigned>(i->ident.vendor), static_cast<unsigned>(i->ident.device),
                     static_cast<unsigned>(i->ident.dclass),
                     static_cast<unsigned>(i->ident.subclass),
                     static_cast<unsigned>(i->ident.protocol));
        }
        if (!s || !i ||
            (identify(i->ident.vendor, i->ident.device, i->ident.dclass, i->ident.subclass,
                      i->ident.protocol) == Unsupported &&
             identify(i->ident.vendor, i->ident.device, 3, 0, 0) == Unsupported &&
             !compositeGipCandidate(i->ident))) {
            return;
        }
        core::LockGuard lock(s->mutex_);
        if (Blocked *b = s->blockedEntry(*i)) {
            b->seen = true; // still there: retried on its own schedule
            return;
        }
        for (unsigned n = 0; n < s->count_; ++n) {
            if (same(s->candidates_[n], *i)) {
                return;
            }
        }
        if (s->count_ < 8) {
            // Pop from the end, maintaining first-in selection.
            memmove(s->candidates_ + 1, s->candidates_, s->count_ * sizeof(*i));
            s->candidates_[0] = *i;
            ++s->count_;
        }
    }
    static void remove(usbd_connection *, usbd_device_instance_t *i) {
        QnxUsb *s = callbackOwner;
        if (!s || !i) {
            return;
        }
        core::LockGuard lock(s->mutex_);
        for (unsigned n = 0; n < s->blockedCount_;) {
            if (same(s->blocked_[n].instance, *i)) {
                s->blocked_[n] = s->blocked_[--s->blockedCount_];
            } else {
                ++n;
            }
        }
        if (s->attached_ && same(s->active_, *i)) {
            s->removed_ = true;
        }
        for (unsigned n = 0; n < s->count_;) {
            if (same(s->candidates_[n], *i)) {
                s->candidates_[n] = s->candidates_[--s->count_];
            } else {
                ++n;
            }
        }
    }
    mutable core::Mutex mutex_;
    usbd_connection *connection_;
    UsbDevice device_;
    usbd_device_instance_t candidates_[8], active_;
    unsigned count_;
    bool attached_, removed_;
    usbd_funcs_t funcs_;
    usbd_device_ident_t ids_;
    Blocked blocked_[8];
    unsigned blockedCount_;
    int lastError_;
    uint64_t connectedAt_;
};
} // namespace
std::unique_ptr<UsbTransport> makeUsbTransport() {
    return std::unique_ptr<UsbTransport>(new (std::nothrow) QnxUsb());
}
} // namespace gamepad
} // namespace nbt
#else
namespace nbt {
namespace gamepad {
std::unique_ptr<UsbTransport> makeUsbTransport() {
    return std::unique_ptr<UsbTransport>();
}
} // namespace gamepad
} // namespace nbt
#endif
