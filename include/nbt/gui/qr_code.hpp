// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_QR_CODE_HPP
#define NBT_GUI_QR_CODE_HPP

#include "nbt/core/status.hpp"
#include "nbt/gui/frame.hpp"

#include <stddef.h>

namespace nbt {
namespace gui {

static const unsigned kQrMaxVersion = 10;
static const unsigned kQrMaxSize = 17 + 4 * kQrMaxVersion;
static const unsigned kQrQuiet = 4;

class QrCode {
public:
    QrCode();
    ~QrCode();
    QrCode(const QrCode &) = delete;
    QrCode &operator=(const QrCode &) = delete;

    core::Status encode(const unsigned char *data, size_t length, int mask);
    bool dark(unsigned x, unsigned y) const;
    core::Status render(Frame &frame, size_t x, size_t y, size_t scale) const;
    void clear();
    bool empty() const {
        return size_ == 0;
    }
    unsigned size() const {
        return size_;
    }

    unsigned version_;
    unsigned size_;
    unsigned mask_;
    unsigned char modules_[kQrMaxSize][kQrMaxSize];
};

} // namespace gui
} // namespace nbt

#endif
