// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/volume_osd.hpp"

#include "nbt/display/layer_manager.hpp"
#include "nbt/log.h"
#include "nbt/most_input.h"
#include "volume_glyphs.hpp"

#include <string.h>
#include <unistd.h>

namespace nbt {
namespace gui {
namespace {

const uint64_t kHideMs = 1400u;
const uint64_t kProbeMs = 300u;
const uint64_t kWaitSliceMs = 100u;
const unsigned kMaxProbes = 10u;

// QNX SCREEN_FORMAT_RGBA8888 is byte-order R, G, B, A on little-endian ARM.
constexpr uint32_t rgba(int r, int g, int b, int a) {
    return (static_cast<uint32_t>(a & 0xff) << 24) | (static_cast<uint32_t>(b & 0xff) << 16) |
           (static_cast<uint32_t>(g & 0xff) << 8) | static_cast<uint32_t>(r & 0xff);
}

const uint32_t kPill = rgba(0x08, 0x0a, 0x0d, 0xd1);
const uint32_t kBorder = rgba(0xff, 0xff, 0xff, 0x24);
const uint32_t kTrack = rgba(0x55, 0x55, 0x55, 0xff);
const uint32_t kGlyph = rgba(0xcf, 0xda, 0xe6, 0xff);
const uint32_t kText = rgba(0xe8, 0xee, 0xf4, 0xff);

int isqrtInt(int v) {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) {
        ++r;
    }
    return r;
}

void fillRect(Frame &f, int x0, int y0, int x1, int y1, uint32_t col) {
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > static_cast<int>(f.width)) {
        x1 = static_cast<int>(f.width);
    }
    if (y1 > static_cast<int>(f.height)) {
        y1 = static_cast<int>(f.height);
    }
    for (int y = y0; y < y1; ++y) {
        uint32_t *row = reinterpret_cast<uint32_t *>(f.pixels + static_cast<size_t>(y) * f.stride);
        for (int x = x0; x < x1; ++x) {
            row[x] = col;
        }
    }
}

void fillPill(Frame &f, int x0, int y0, int x1, int y1, uint32_t col) {
    const int h = y1 - y0;
    const int r = h / 2;
    if (r < 1) {
        fillRect(f, x0, y0, x1, y1, col);
        return;
    }
    for (int dy = 0; dy < h; ++dy) {
        const int off = dy < r ? (r - dy) : (dy - r + 1);
        const int dx = r - isqrtInt(r * r - off * off);
        fillRect(f, x0 + dx, y0 + dy, x1 - dx, y0 + dy + 1, col);
    }
}

uint32_t barColor(int pct) {
    static const int kStops[4][4] = {
        {0, 0x2f, 0xa8, 0x4f},
        {35, 0x7b, 0xc0, 0x43},
        {70, 0xf2, 0xa2, 0x1e},
        {100, 0xe0, 0x3b, 0x30},
    };
    int i = 0;
    while (i < 2 && pct > kStops[i + 1][0]) {
        ++i;
    }
    const int span = kStops[i + 1][0] - kStops[i][0];
    const int t = span > 0 ? (pct - kStops[i][0]) * 255 / span : 255;
    return rgba(kStops[i][1] + (kStops[i + 1][1] - kStops[i][1]) * t / 255,
                kStops[i][2] + (kStops[i + 1][2] - kStops[i][2]) * t / 255,
                kStops[i][3] + (kStops[i + 1][3] - kStops[i][3]) * t / 255, 0xff);
}

void drawBitmap(Frame &f, int x, int y, int cols, int scale, const uint8_t rows[7], uint32_t col) {
    for (int r = 0; r < 7; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (rows[r] & (1u << (cols - 1 - c))) {
                fillRect(f, x + c * scale, y + r * scale, x + (c + 1) * scale, y + (r + 1) * scale,
                         col);
            }
        }
    }
}

} // namespace

VolumeOsd::VolumeOsd()
    : window_(), volumeFd_(-1), started_(false), overlayReady_(false), overlayFailed_(false),
      shown_(false), pending_(false), pct_(0), drawnPct_(-1), lastSeq_(0), probeCount_(0),
      nextProbeMs_(0), hideAtMs_(0) {
}

VolumeOsd::~VolumeOsd() {
    stop();
}

bool VolumeOsd::start(int panelWidth, int panelHeight) {
    stop();
    if (panelWidth <= 0 || panelHeight <= 0) {
        return false;
    }
    if (!window_
             .open(panelWidth, panelHeight, display::kVolumeSurface, true, panelWidth, panelHeight)
             .ok()) {
        return false;
    }
    started_ = true;
    attachInput();
    clearSurface();
    return true;
}

void VolumeOsd::attachInput() {
    if (!started_ || volumeFd_ >= 0) {
        return;
    }
    volumeFd_ = nbt_most_input_volume_fd();
    if (volumeFd_ >= 0) {
        int ignoredPct = 0;
        (void)nbt_most_input_volume(&ignoredPct, &lastSeq_);
    }
}

void VolumeOsd::stop() {
    if (started_ && shown_) {
        clearSurface();
    }
    window_.close();
    volumeFd_ = -1;
    started_ = false;
    overlayReady_ = false;
    overlayFailed_ = false;
    shown_ = false;
    pending_ = false;
    drawnPct_ = -1;
    probeCount_ = 0;
    nextProbeMs_ = 0;
    hideAtMs_ = 0;
}

void VolumeOsd::onVolume(int pct, uint64_t nowMs) {
    pct_ = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    pending_ = true;
    hideAtMs_ = nowMs + kHideMs;
}

void VolumeOsd::clearSurface() {
    Frame &f = window_.frame();
    if (f.valid()) {
        memset(f.pixels, 0, f.bytes);
        (void)window_.post();
    }
    shown_ = false;
    drawnPct_ = -1;
}

void VolumeOsd::renderPill(int pct) {
    Frame &f = window_.frame();
    if (!f.valid()) {
        return;
    }
    memset(f.pixels, 0, f.bytes);
    const int pw = static_cast<int>(f.width);
    const int ph = static_cast<int>(f.height);
    const int scale = ph >= 520 ? 3 : 2;
    const int w = ph >= 520 ? 420 : 340;
    const int h = ph >= 520 ? 64 : 52;
    const int margin = ph >= 520 ? 28 : 22;
    const int x0 = (pw - w) / 2;
    const int y0 = ph - h - margin;
    fillPill(f, x0, y0, x0 + w, y0 + h, kBorder);
    fillPill(f, x0 + 1, y0 + 1, x0 + w - 1, y0 + h - 1, kPill);

    const int pad = 22;
    const int gap = 16;
    const int cy = y0 + h / 2;
    drawBitmap(f, x0 + pad, cy - (7 * scale) / 2, 7, scale, kVolumeSpeaker, kGlyph);

    const int digitW = 5 * scale;
    const int digitAdvance = digitW + scale;
    const int numBox = 3 * digitAdvance;
    const int barX0 = x0 + pad + 7 * scale + gap;
    const int barX1 = x0 + w - pad - numBox - gap;
    const int barH = scale * 3;
    const int barY = cy - barH / 2;
    fillPill(f, barX0, barY, barX1, barY + barH, kTrack);
    const int fillW = (barX1 - barX0) * pct / 100;
    if (fillW >= 2) {
        fillPill(f, barX0, barY, barX0 + fillW, barY + barH, barColor(pct));
    }

    int d[3] = {0, 0, 0};
    int len = 0;
    if (pct >= 100) {
        d[0] = 1;
        d[1] = 0;
        d[2] = 0;
        len = 3;
    } else if (pct >= 10) {
        d[0] = pct / 10;
        d[1] = pct % 10;
        len = 2;
    } else {
        d[0] = pct;
        len = 1;
    }
    int tx = x0 + w - pad - len * digitAdvance + scale;
    for (int i = 0; i < len; ++i) {
        drawBitmap(f, tx, cy - (7 * scale) / 2, 5, scale, kVolumeDigits[d[i]], kText);
        tx += digitAdvance;
    }
    if (window_.post().ok()) {
        shown_ = true;
        drawnPct_ = pct;
    }
}

void VolumeOsd::service(uint64_t nowMs, display::Placement &panel) {
    if (!started_) {
        return;
    }
    if (volumeFd_ >= 0) {
        char drain[32];
        while (read(volumeFd_, drain, sizeof(drain)) > 0) {
        }
        int pct = 0;
        uint32_t seq = 0;
        if (nbt_most_input_volume(&pct, &seq) && seq != lastSeq_) {
            lastSeq_ = seq;
            onVolume(pct, nowMs);
        }
    }
    if (panel.state() != display::Placement::OnPanel) {
        pending_ = false;
        if (shown_) {
            clearSurface();
        }
        return;
    }
    if (!overlayReady_ && !overlayFailed_ && nowMs >= nextProbeMs_) {
        nextProbeMs_ = nowMs + kProbeMs;
        if (display::surfaceBound(display::kVolumeSurface)) {
            if (display::attachOverlay(display::kVolumeSurface, panel.surface())) {
                overlayReady_ = true;
                panel.setOverlay(display::kVolumeSurface);
                nbt_logf("volume OSD: surface %d on top of surface %d", display::kVolumeSurface,
                         panel.surface());
            } else {
                overlayFailed_ = true;
                nbt_logf("volume OSD: LayerManager refused overlay order; disabled");
            }
        } else if (++probeCount_ >= kMaxProbes) {
            overlayFailed_ = true;
            nbt_logf("volume OSD: surface %d did not appear", display::kVolumeSurface);
        }
    }
    if (pending_) {
        pending_ = false;
        if (pct_ != drawnPct_) {
            renderPill(pct_);
        }
    } else if (shown_ && nowMs >= hideAtMs_) {
        clearSurface();
    }
}

uint64_t VolumeOsd::nextWakeMs(uint64_t nowMs) const {
    if (!started_) {
        return 0;
    }
    if (shown_) {
        return nowMs < hideAtMs_
                   ? (hideAtMs_ - nowMs < kWaitSliceMs ? hideAtMs_ - nowMs : kWaitSliceMs)
                   : 1u;
    }
    return 0;
}

} // namespace gui
} // namespace nbt
