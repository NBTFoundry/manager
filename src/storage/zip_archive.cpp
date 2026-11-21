// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/zip_archive.hpp"

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace nbt {
namespace storage {
namespace {

const uint32_t kEndSignature = 0x06054b50;
const uint32_t kCentralSignature = 0x02014b50;
const uint32_t kLocalSignature = 0x04034b50;
const size_t kEndMin = 22;
const size_t kCommentMax = 65535;
const size_t kChunk = 16384;

uint32_t u16(const unsigned char *p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8));
}

uint32_t u32(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool writeAll(int fd, const unsigned char *data, size_t size) {
    while (size) {
        const ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

} // namespace

ZipArchive::ZipArchive() : fd_(-1), fileSize_(0), count_(0), entries_(NULL) {
}

ZipArchive::~ZipArchive() {
    close();
}

void ZipArchive::close() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = -1;
    count_ = 0;
    delete[] entries_;
    entries_ = NULL;
}

const char *ZipArchive::baseName(const char *name) {
    const char *base = name;
    for (const char *p = name; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

bool ZipArchive::readAt(uint64_t offset, unsigned char *data, size_t size) {
    if (offset > fileSize_ || size > fileSize_ - offset) {
        return false;
    }
    size_t done = 0;
    while (done < size) {
        const ssize_t n = pread(fd_, data + done, size - done, static_cast<off_t>(offset + done));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return true;
}

bool ZipArchive::open(const char *path) {
    close();
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd_ = ::open(path, flags);
    struct stat st;
    if (fd_ < 0 || fstat(fd_, &st) || !S_ISREG(st.st_mode) ||
        static_cast<uint64_t>(st.st_size) < kEndMin) {
        close();
        return false;
    }
    fileSize_ = static_cast<uint64_t>(st.st_size);
    // The end record, after which only its comment may come.
    const size_t tail =
        static_cast<size_t>(fileSize_ < kEndMin + kCommentMax ? fileSize_ : kEndMin + kCommentMax);
    unsigned char *buffer = new (std::nothrow) unsigned char[tail];
    bool found = buffer && readAt(fileSize_ - tail, buffer, tail);
    uint32_t total = 0, size = 0, offset = 0;
    if (found) {
        found = false;
        for (size_t at = tail - kEndMin + 1; at-- > 0;) {
            const unsigned char *end = buffer + at;
            if (u32(end) == kEndSignature && at + kEndMin + u16(end + 20) == tail) {
                // One disk only; 0xffff / 0xffffffff would mean zip64.
                found = !u16(end + 4) && !u16(end + 6) && u16(end + 8) == u16(end + 10) &&
                        u16(end + 10) != 0xffff && u32(end + 12) != 0xffffffffu &&
                        u32(end + 16) != 0xffffffffu;
                total = u16(end + 10);
                size = u32(end + 12);
                offset = u32(end + 16);
                break;
            }
        }
    }
    delete[] buffer;
    if (!found || !central(offset, size, total)) {
        close();
        return false;
    }
    return true;
}

bool ZipArchive::central(uint64_t offset, uint64_t size, unsigned total) {
    if (total > kEntriesMax || size > kDirectoryMax || size > fileSize_ ||
        offset > fileSize_ - size) {
        return false;
    }
    entries_ = new (std::nothrow) ZipEntry[total ? total : 1];
    unsigned char *directory = new (std::nothrow) unsigned char[size ? size : 1];
    bool ok = entries_ && directory && readAt(offset, directory, static_cast<size_t>(size));
    size_t at = 0;
    for (unsigned i = 0; ok && i < total; ++i) {
        const unsigned char *p = directory + at;
        if (size - at < 46 || u32(p) != kCentralSignature) {
            ok = false;
            break;
        }
        const uint32_t flags = u16(p + 8), method = u16(p + 10);
        const uint32_t nameLength = u16(p + 28), extra = u16(p + 30), comment = u16(p + 32);
        if (size - at < 46 + nameLength + extra + comment) {
            ok = false;
            break;
        }
        ZipEntry &e = entries_[count_];
        const bool usable = !(flags & 1) && (method == 0 || method == 8) && nameLength &&
                            nameLength < sizeof(e.name) && u32(p + 20) != 0xffffffffu &&
                            u32(p + 24) != 0xffffffffu && u32(p + 42) != 0xffffffffu &&
                            p[46 + nameLength - 1] != '/' && !memchr(p + 46, 0, nameLength);
        if (usable) {
            memcpy(e.name, p + 46, nameLength);
            e.name[nameLength] = 0;
            e.method = method;
            e.crc = u32(p + 16);
            e.packed = u32(p + 20);
            e.size = u32(p + 24);
            e.offset = u32(p + 42);
            ++count_;
        }
        at += 46 + nameLength + extra + comment;
    }
    delete[] directory;
    if (!ok) {
        count_ = 0;
    }
    return ok;
}

int ZipArchive::extract(unsigned i, int out, void (*progress)(void *, uint64_t), void *context) {
    if (i >= count_) {
        return -1;
    }
    const ZipEntry &e = entries_[i];
    unsigned char local[30];
    if (!readAt(e.offset, local, sizeof(local)) || u32(local) != kLocalSignature) {
        return -1;
    }
    uint64_t at = e.offset + 30 + u16(local + 26) + u16(local + 28);
    if (at > fileSize_ || e.packed > fileSize_ - at) {
        return -1;
    }
    unsigned char *in = new (std::nothrow) unsigned char[2 * kChunk];
    if (!in) {
        return -1;
    }
    unsigned char *output = in + kChunk;
    z_stream z;
    memset(&z, 0, sizeof(z));
    bool ok = e.method == 0 || inflateInit2(&z, -MAX_WBITS) == Z_OK;
    const bool inflating = ok && e.method == 8;
    uint64_t left = e.packed, written = 0;
    uLong crc = crc32(0L, Z_NULL, 0);
    bool ended = false;
    while (ok && !ended) {
        const size_t want = left < kChunk ? static_cast<size_t>(left) : kChunk;
        if (want && !readAt(at, in, want)) {
            ok = false;
            break;
        }
        at += want;
        left -= want;
        if (!inflating) {
            ok = writeAll(out, in, want);
            crc = crc32(crc, in, static_cast<uInt>(want));
            written += want;
            if (progress && want) {
                progress(context, want);
            }
            ended = !left;
            continue;
        }
        z.next_in = in;
        z.avail_in = static_cast<uInt>(want);
        do {
            z.next_out = output;
            z.avail_out = static_cast<uInt>(kChunk);
            const int status = inflate(&z, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
                ok = false;
                break;
            }
            const size_t produced = kChunk - z.avail_out;
            ok = writeAll(out, output, produced);
            crc = crc32(crc, output, static_cast<uInt>(produced));
            written += produced;
            if (progress && produced) {
                progress(context, produced);
            }
            if (status == Z_STREAM_END) {
                ended = true;
                break;
            }
            if (status == Z_BUF_ERROR && !z.avail_in && !left) {
                ok = false; // the data ended before the stream
                break;
            }
        } while (ok && (z.avail_in || !z.avail_out));
    }
    if (inflating) {
        inflateEnd(&z);
    }
    delete[] in;
    return ok && written == e.size && static_cast<uint32_t>(crc) == e.crc ? 0 : -1;
}

} // namespace storage
} // namespace nbt
