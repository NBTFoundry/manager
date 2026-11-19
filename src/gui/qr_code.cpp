// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/qr_code.hpp"

#include "nbt/core/secure.hpp"

#include "nbt/util.h"
#include <errno.h>

#include <string.h>

namespace nbt {
namespace gui {

#define DARK 1u
#define FUNCTION 2u
#define MAX_CODEWORDS 346
#define MAX_EC 26

/* Error correction level M, indexed by version. */
static const unsigned short total_codewords[kQrMaxVersion + 1] = {0,   26,  44,  70,  100, 134,
                                                                  172, 196, 242, 292, 346};
static const unsigned char ec_per_block[kQrMaxVersion + 1] = {0,  10, 16, 26, 18, 24,
                                                              16, 18, 22, 22, 26};
static const unsigned char blocks[kQrMaxVersion + 1] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
static const unsigned char alignment[kQrMaxVersion + 1][3] = {
    {0},     {0},         {6, 18},     {6, 22},     {6, 26},    {6, 30},
    {6, 34}, {6, 22, 38}, {6, 24, 42}, {6, 26, 46}, {6, 28, 50}};

static unsigned data_codewords(unsigned version) {
    return total_codewords[version] - (unsigned)ec_per_block[version] * blocks[version];
}

static unsigned char gf_multiply(unsigned char a, unsigned char b) {
    unsigned result = 0, x = a;
    while (b) {
        if (b & 1) {
            result ^= x;
        }
        x <<= 1;
        if (x & 0x100) {
            x ^= 0x11d;
        }
        b >>= 1;
    }
    return (unsigned char)result;
}

/* Generator polynomial coefficients, highest degree first, leading 1 omitted. */
static void rs_generator(unsigned degree, unsigned char *out) {
    unsigned i, j;
    unsigned char root = 1;
    memset(out, 0, degree);
    out[degree - 1] = 1;
    for (i = 0; i < degree; ++i) {
        for (j = 0; j < degree; ++j) {
            out[j] = gf_multiply(out[j], root);
            if (j + 1 < degree) {
                out[j] ^= out[j + 1];
            }
        }
        root = gf_multiply(root, 2);
    }
}

static void rs_remainder(const unsigned char *data, unsigned length, const unsigned char *generator,
                         unsigned degree, unsigned char *out) {
    unsigned i, j;
    memset(out, 0, degree);
    for (i = 0; i < length; ++i) {
        unsigned char factor = data[i] ^ out[0];
        memmove(out, out + 1, degree - 1);
        out[degree - 1] = 0;
        for (j = 0; j < degree; ++j) {
            out[j] ^= gf_multiply(generator[j], factor);
        }
    }
}

static void put(QrCode *qr, unsigned x, unsigned y, int dark, int function) {
    qr->modules_[y][x] = (unsigned char)((dark ? DARK : 0u) | (function ? FUNCTION : 0u));
}

static void finder(QrCode *qr, int cx, int cy) {
    int dx, dy;
    for (dy = -4; dy <= 4; ++dy) {
        for (dx = -4; dx <= 4; ++dx) {
            int x = cx + dx, y = cy + dy, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            int distance = ax > ay ? ax : ay;
            if (x < 0 || y < 0 || x >= (int)qr->size_ || y >= (int)qr->size_) {
                continue;
            }
            put(qr, (unsigned)x, (unsigned)y, distance != 2 && distance != 4, 1);
        }
    }
}

static void format_bits(QrCode *qr) {
    unsigned data = (0u << 3) | qr->mask_, remainder = data, bits, i,
             n = qr->size_; /* level M = 00 */
    for (i = 0; i < 10; ++i) {
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537u);
    }
    bits = ((data << 10) | (remainder & 0x3ffu)) ^ 0x5412u;
    for (i = 0; i <= 5; ++i) {
        put(qr, 8, i, (bits >> i) & 1, 1);
    }
    put(qr, 8, 7, (bits >> 6) & 1, 1);
    put(qr, 8, 8, (bits >> 7) & 1, 1);
    put(qr, 7, 8, (bits >> 8) & 1, 1);
    for (i = 9; i < 15; ++i) {
        put(qr, 14 - i, 8, (bits >> i) & 1, 1);
    }
    for (i = 0; i < 8; ++i) {
        put(qr, n - 1 - i, 8, (bits >> i) & 1, 1);
    }
    for (i = 8; i < 15; ++i) {
        put(qr, 8, n - 15 + i, (bits >> i) & 1, 1);
    }
    put(qr, 8, n - 8, 1, 1);
}

static void function_patterns(QrCode *qr) {
    unsigned i, j, count = qr->version_ >= 7 ? 3 : qr->version_ >= 2 ? 2 : 0, n = qr->size_;
    for (i = 0; i < n; ++i) {
        put(qr, 6, i, i % 2 == 0, 1);
        put(qr, i, 6, i % 2 == 0, 1);
    }
    finder(qr, 3, 3);
    finder(qr, (int)n - 4, 3);
    finder(qr, 3, (int)n - 4);
    for (i = 0; i < count; ++i) {
        for (j = 0; j < count; ++j) {
            int dx, dy;
            if ((i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0)) {
                continue;
            }
            for (dy = -2; dy <= 2; ++dy) {
                for (dx = -2; dx <= 2; ++dx) {
                    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
                    put(qr, (unsigned)(alignment[qr->version_][i] + dx),
                        (unsigned)(alignment[qr->version_][j] + dy), (ax > ay ? ax : ay) != 1, 1);
                }
            }
        }
    }
    format_bits(qr); /* reserves the format areas; rewritten once the mask is known */
    if (qr->version_ >= 7) {
        unsigned remainder = qr->version_, bits;
        for (i = 0; i < 12; ++i) {
            remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1f25u);
        }
        bits = (qr->version_ << 12) | (remainder & 0xfffu);
        for (i = 0; i < 18; ++i) {
            unsigned a = n - 11 + i % 3, b = i / 3;
            put(qr, a, b, (bits >> i) & 1, 1);
            put(qr, b, a, (bits >> i) & 1, 1);
        }
    }
}

static void place(QrCode *qr, const unsigned char *codewords, unsigned count) {
    unsigned bit = 0, total = count * 8, vertical, j, n = qr->size_;
    int right;
    for (right = (int)n - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right = 5;
        }
        for (vertical = 0; vertical < n; ++vertical) {
            for (j = 0; j < 2; ++j) {
                unsigned x = (unsigned)right - j;
                unsigned y = ((right + 1) & 2) == 0 ? n - 1 - vertical : vertical;
                if (qr->modules_[y][x] & FUNCTION) {
                    continue;
                }
                if (bit < total) {
                    qr->modules_[y][x] = (codewords[bit >> 3] >> (7 - (bit & 7))) & 1;
                }
                ++bit;
            }
        }
    }
}

static int masked(unsigned mask, unsigned x, unsigned y) {
    switch (mask) {
    case 0:
        return (x + y) % 2 == 0;
    case 1:
        return y % 2 == 0;
    case 2:
        return x % 3 == 0;
    case 3:
        return (x + y) % 3 == 0;
    case 4:
        return (x / 3 + y / 2) % 2 == 0;
    case 5:
        return x * y % 2 + x * y % 3 == 0;
    case 6:
        return (x * y % 2 + x * y % 3) % 2 == 0;
    default:
        return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

static void apply_mask(QrCode *qr, unsigned mask) {
    unsigned x, y;
    for (y = 0; y < qr->size_; ++y) {
        for (x = 0; x < qr->size_; ++x) {
            if (!(qr->modules_[y][x] & FUNCTION) && masked(mask, x, y)) {
                qr->modules_[y][x] ^= DARK;
            }
        }
    }
}

static int module_at(const QrCode *qr, unsigned x, unsigned y, int column) {
    return column ? qr->modules_[x][y] & DARK : qr->modules_[y][x] & DARK;
}

/* Penalty rules from the QR specification; used only to choose a mask. */
static unsigned long penalty(const QrCode *qr) {
    static const unsigned char pattern[2][11] = {{1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0},
                                                 {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1}};
    unsigned long score = 0, dark = 0, total = (unsigned long)qr->size_ * qr->size_, k;
    unsigned n = qr->size_, a, b, run, p, i;
    int column;
    for (column = 0; column < 2; ++column) {
        for (a = 0; a < n; ++a) {
            run = 1;
            for (b = 1; b <= n; ++b) {
                if (b < n && module_at(qr, b, a, column) == module_at(qr, b - 1, a, column)) {
                    ++run;
                    continue;
                }
                if (run >= 5) {
                    score += 3 + (run - 5);
                }
                run = 1;
            }
            for (b = 0; b + 11 <= n; ++b) {
                for (p = 0; p < 2; ++p) {
                    for (i = 0; i < 11 && module_at(qr, b + i, a, column) == pattern[p][i]; ++i) {
                    }
                    if (i == 11) {
                        score += 40;
                    }
                }
            }
        }
    }
    for (a = 0; a + 1 < n; ++a) {
        for (b = 0; b + 1 < n; ++b) {
            unsigned c = qr->modules_[a][b] & DARK;
            if ((qr->modules_[a][b + 1] & DARK) == c && (qr->modules_[a + 1][b] & DARK) == c &&
                (qr->modules_[a + 1][b + 1] & DARK) == c) {
                score += 3;
            }
        }
    }
    for (a = 0; a < n; ++a) {
        for (b = 0; b < n; ++b) {
            dark += qr->modules_[a][b] & DARK;
        }
    }
    k = dark * 20 > total * 10 ? dark * 20 - total * 10 : total * 10 - dark * 20;
    k = (k + total - 1) / total;
    return score + (k ? k - 1 : 0) * 10;
}

static void append_bits(unsigned char *out, unsigned *bit, unsigned value, unsigned count) {
    while (count-- > 0) {
        if ((value >> count) & 1u) {
            out[*bit >> 3] |= (unsigned char)(0x80u >> (*bit & 7));
        }
        ++*bit;
    }
}

core::Status QrCode::encode(const unsigned char *data, size_t length, int mask) {
    QrCode *qr = this;
    unsigned char codewords[MAX_CODEWORDS], interleaved[MAX_CODEWORDS];
    unsigned char generator[MAX_EC], ec[kQrMaxVersion / 2][MAX_EC];
    unsigned version, capacity, count_bits, bit = 0, i, j, b, best = 0, short_blocks, short_length;
    unsigned ec_length, block_count, offset, used = 0;
    unsigned long best_score = 0;
    if (!qr || (!data && length) || mask < -1 || mask > 7) {
        return core::Status::error(EINVAL, "QR operation failed");
    }
    for (version = 1; version <= kQrMaxVersion; ++version) {
        count_bits = version < 10 ? 8 : 16;
        if (length <= (data_codewords(version) * 8 - 4 - count_bits) / 8) {
            break;
        }
    }
    if (version > kQrMaxVersion) {
        return core::Status::error(EINVAL, "QR operation failed");
    }
    clear();
    memset(codewords, 0, sizeof(codewords));
    qr->version_ = version;
    qr->size_ = 17 + 4 * version;
    capacity = data_codewords(version);
    append_bits(codewords, &bit, 4u, 4);
    append_bits(codewords, &bit, (unsigned)length, count_bits);
    for (i = 0; i < length; ++i) {
        append_bits(codewords, &bit, data[i], 8);
    }
    for (i = 0; i < 4 && bit < capacity * 8; ++i) {
        ++bit;
    }
    bit = (bit + 7) & ~7u;
    for (i = 0; bit < capacity * 8; ++i, bit += 8) {
        codewords[bit >> 3] = i % 2 ? 0x11 : 0xec;
    }
    /* Split into blocks, append error correction and interleave. */
    block_count = blocks[version];
    ec_length = ec_per_block[version];
    short_blocks = block_count - total_codewords[version] % block_count;
    short_length = total_codewords[version] / block_count;
    rs_generator(ec_length, generator);
    for (b = 0, offset = 0; b < block_count; ++b) {
        unsigned length_b = short_length - ec_length + (b >= short_blocks);
        rs_remainder(codewords + offset, length_b, generator, ec_length, ec[b]);
        offset += length_b;
    }
    for (i = 0; i <= short_length - ec_length; ++i) {
        for (b = 0, offset = 0; b < block_count; ++b) {
            unsigned length_b = short_length - ec_length + (b >= short_blocks);
            if (i < length_b) {
                interleaved[used++] = codewords[offset + i];
            }
            offset += length_b;
        }
    }
    for (i = 0; i < ec_length; ++i) {
        for (b = 0; b < block_count; ++b) {
            interleaved[used++] = ec[b][i];
        }
    }
    function_patterns(qr);
    place(qr, interleaved, used);
    if (mask < 0) {
        for (j = 0; j < 8; ++j) {
            unsigned long score;
            qr->mask_ = j;
            apply_mask(qr, j);
            format_bits(qr);
            score = penalty(qr);
            if (j == 0 || score < best_score) {
                best = j;
                best_score = score;
            }
            apply_mask(qr, j);
        }
        mask = (int)best;
    }
    qr->mask_ = (unsigned)mask;
    apply_mask(qr, qr->mask_);
    format_bits(qr);
    nbt_clear(codewords, sizeof(codewords));
    nbt_clear(interleaved, sizeof(interleaved));
    nbt_clear(ec, sizeof(ec));
    return core::Status();
}

bool QrCode::dark(unsigned x, unsigned y) const {
    const QrCode *qr = this;
    return qr != NULL && x < qr->size_ && y < qr->size_ && (qr->modules_[y][x] & DARK);
}

core::Status QrCode::render(Frame &frame, size_t x, size_t y, size_t scale) const {
    const QrCode *qr = this;
    Frame *f = &frame;
    const uint32_t light = 0xffffffffu, darkColor = 0xff000000u;
    size_t side, row, col, my, mx;
    if (!qr || !qr->size_ || qr->size_ > kQrMaxSize || !f->valid() || !scale) {
        return core::Status::error(EINVAL, "QR operation failed");
    }
    side = qr->size_ + 2 * kQrQuiet;
    if (scale > f->width / side || scale > f->height / side || x > f->width - side * scale ||
        y > f->height - side * scale) {
        return core::Status::error(EINVAL, "QR operation failed");
    }
    for (row = 0; row < side * scale; ++row) {
        unsigned char *line = f->pixels + (y + row) * f->stride + x * 4;
        my = row / scale;
        for (col = 0; col < side * scale; ++col) {
            int on;
            mx = col / scale;
            on = my >= kQrQuiet && mx >= kQrQuiet &&
                 dark((unsigned)(mx - kQrQuiet), (unsigned)(my - kQrQuiet));
            memcpy(line + col * 4, on ? &darkColor : &light, 4);
        }
    }
    return core::Status();
}

void QrCode::clear() {
    version_ = 0;
    size_ = 0;
    mask_ = 0;
    core::secureClear(modules_, sizeof(modules_));
}

QrCode::QrCode() : version_(0), size_(0), mask_(0), modules_() {
}
QrCode::~QrCode() {
    clear();
}

} // namespace gui
} // namespace nbt
