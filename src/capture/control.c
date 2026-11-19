/* Serialization, validation, and checked shared-memory I/O for capture state. */
#include "nbt/capture/control.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int parse_u64_exact(const char *s, size_t len, size_t *pos, uint64_t *out) {
    size_t i = *pos;
    uint64_t val = 0;
    size_t digits = 0;
    if (i >= len || s[i] < '0' || s[i] > '9') {
        return -1;
    }
    if (s[i] == '0' && i + 1 < len && s[i + 1] >= '0' && s[i + 1] <= '9') {
        return -1; /* disallow leading zero on multi-digit integers */
    }
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        const unsigned d = (unsigned)(s[i] - '0');
        if (digits >= 20u || val > (UINT64_MAX - d) / 10u) {
            return -1;
        }
        val = val * 10u + d;
        ++i;
        ++digits;
    }
    *pos = i;
    *out = val;
    return 0;
}

static int parse_hex_u8(const char *s, size_t len, size_t *pos, uint8_t *out) {
    size_t i = *pos;
    unsigned v = 0;
    size_t digits = 0;
    while (i < len && digits < 2u) {
        char c = s[i];
        unsigned d = 0;
        if (c >= '0' && c <= '9') {
            d = (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (unsigned)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (unsigned)(c - 'A' + 10);
        } else {
            break;
        }
        v = (v << 4) | d;
        ++i;
        ++digits;
    }
    if (digits != 2u || v > 0xFFu) {
        return -1;
    }
    *pos = i;
    *out = (uint8_t)v;
    return 0;
}

static int expect_lit(const char *s, size_t len, size_t *pos, const char *lit) {
    const size_t n = strlen(lit);
    if (*pos + n > len || memcmp(s + *pos, lit, n) != 0) {
        return -1;
    }
    *pos += n;
    return 0;
}

static int ccs_is_valid(const nbt_ccs_config *c) {
    int knob = 0, ccs = 0;
    size_t i, j;
    if (!c || !c->instance_count || c->instance_count > NBT_CCS_INSTANCES_MAX ||
        c->knob_menu_slot >= NBT_CCS_SLOTS || c->knob_back_slot >= NBT_CCS_SLOTS ||
        c->ccs_menu_slot >= NBT_CCS_SLOTS || c->ccs_back_slot >= NBT_CCS_SLOTS ||
        c->knob_menu_slot == c->knob_back_slot || c->ccs_menu_slot == c->ccs_back_slot) {
        return 0;
    }
    for (i = 0; i < c->instance_count; ++i) {
        for (j = 0; j < i; ++j) {
            if (c->instances[i] == c->instances[j]) {
                return 0;
            }
        }
        if (c->instances[i] == c->knob_instance) {
            knob = 1;
        }
        if (c->instances[i] == c->ccs_instance) {
            ccs = 1;
        }
    }
    return knob && ccs;
}

void nbt_capture_control_init(nbt_capture_control *ctrl, int32_t gui_pid, uint64_t session_id,
                              uint64_t generation, const nbt_ccs_config *ccs) {
    static const uint8_t kDefaultInstances[] = {0x01, 0x02, 0x03, 0x05, 0x10, 0x12, 0x14};
    if (!ctrl) {
        return;
    }
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->version = NBT_CAPTURE_CONTROL_VERSION;
    ctrl->gui_pid = gui_pid;
    ctrl->session_id = session_id;
    ctrl->generation = generation;
    ctrl->state = NBT_CAPTURE_OFF;
    if (ccs && ccs_is_valid(ccs)) {
        ctrl->knob_instance = ccs->knob_instance;
        ctrl->ccs_instance = ccs->ccs_instance;
        ctrl->instance_count = ccs->instance_count;
        memcpy(ctrl->instances, ccs->instances, ccs->instance_count);
        ctrl->knob_back_slot = ccs->knob_back_slot;
        ctrl->ccs_back_slot = ccs->ccs_back_slot;
    } else {
        ctrl->knob_instance = 0x03u;
        ctrl->ccs_instance = 0x01u;
        ctrl->instance_count = sizeof(kDefaultInstances);
        memcpy(ctrl->instances, kDefaultInstances, sizeof(kDefaultInstances));
        ctrl->knob_back_slot = 1u;
        ctrl->ccs_back_slot = 1u;
    }
    ctrl->watchdog_ms = NBT_CAPTURE_DEFAULT_WATCHDOG_MS;
    ctrl->escape_enabled = 1u;
}

int nbt_capture_control_valid(const nbt_capture_control *c) {
    int knob = 0, ccs = 0;
    size_t i, j;
    if (!c || c->version != NBT_CAPTURE_CONTROL_VERSION || c->gui_pid <= 0 || c->session_id == 0 ||
        c->generation == 0 || c->state > NBT_CAPTURE_ON || c->instance_count == 0 ||
        c->instance_count > NBT_CCS_INSTANCES_MAX || c->knob_back_slot >= NBT_CCS_SLOTS ||
        c->ccs_back_slot >= NBT_CCS_SLOTS || c->watchdog_ms < NBT_CAPTURE_MIN_WATCHDOG_MS ||
        c->watchdog_ms > NBT_CAPTURE_MAX_WATCHDOG_MS || c->escape_enabled > 1u) {
        return 0;
    }
    for (i = 0; i < c->instance_count; ++i) {
        for (j = 0; j < i; ++j) {
            if (c->instances[i] == c->instances[j]) {
                return 0;
            }
        }
        if (c->instances[i] == c->knob_instance) {
            knob = 1;
        }
        if (c->instances[i] == c->ccs_instance) {
            ccs = 1;
        }
    }
    return knob && ccs;
}

int nbt_capture_control_format(const nbt_capture_control *c, char *out, size_t size) {
    size_t used = 0;
    size_t i;
    int n;
    if (!out || size < 16 || !nbt_capture_control_valid(c)) {
        return -1;
    }
    n = snprintf(
        out, size, "v%u pid=%ld sid=%llu gen=%llu st=%u k=%02x c=%02x inst=", (unsigned)c->version,
        (long)c->gui_pid, (unsigned long long)c->session_id, (unsigned long long)c->generation,
        (unsigned)c->state, (unsigned)c->knob_instance, (unsigned)c->ccs_instance);
    if (n < 0 || (size_t)n >= size) {
        return -1;
    }
    used = (size_t)n;
    for (i = 0; i < c->instance_count; ++i) {
        n = snprintf(out + used, size - used, "%s%02x", i ? "," : "", (unsigned)c->instances[i]);
        if (n < 0 || (size_t)n >= size - used) {
            return -1;
        }
        used += (size_t)n;
    }
    n = snprintf(out + used, size - used, " kb=%u cb=%u wd=%u esc=%u\n",
                 (unsigned)c->knob_back_slot, (unsigned)c->ccs_back_slot, (unsigned)c->watchdog_ms,
                 (unsigned)c->escape_enabled);
    if (n < 0 || (size_t)n >= size - used) {
        return -1;
    }
    used += (size_t)n;
    return used >= NBT_CAPTURE_CONTROL_MAX ? -1 : (int)used;
}

int nbt_capture_control_parse(const char *text, size_t size, nbt_capture_control *out) {
    nbt_capture_control tmp;
    size_t pos = 0;
    uint64_t val = 0;
    if (!text || !out || size == 0 || size >= NBT_CAPTURE_CONTROL_MAX ||
        memchr(text, '\0', size) != NULL) {
        return -1;
    }
    memset(&tmp, 0, sizeof(tmp));
    if (expect_lit(text, size, &pos, "v1 pid=") || parse_u64_exact(text, size, &pos, &val) ||
        val == 0 || val > 0x7FFFFFFFu) {
        return -1;
    }
    tmp.version = 1u;
    tmp.gui_pid = (int32_t)val;
    if (expect_lit(text, size, &pos, " sid=") ||
        parse_u64_exact(text, size, &pos, &tmp.session_id) ||
        expect_lit(text, size, &pos, " gen=") ||
        parse_u64_exact(text, size, &pos, &tmp.generation) ||
        expect_lit(text, size, &pos, " st=") || parse_u64_exact(text, size, &pos, &val) ||
        val > 1u) {
        return -1;
    }
    tmp.state = (uint8_t)val;
    if (expect_lit(text, size, &pos, " k=") || parse_hex_u8(text, size, &pos, &tmp.knob_instance) ||
        expect_lit(text, size, &pos, " c=") || parse_hex_u8(text, size, &pos, &tmp.ccs_instance) ||
        expect_lit(text, size, &pos, " inst=")) {
        return -1;
    }
    while (tmp.instance_count < NBT_CCS_INSTANCES_MAX) {
        if (parse_hex_u8(text, size, &pos, &tmp.instances[tmp.instance_count])) {
            return -1;
        }
        ++tmp.instance_count;
        if (pos < size && text[pos] == ',') {
            ++pos;
            if (tmp.instance_count == NBT_CCS_INSTANCES_MAX) {
                return -1;
            }
            continue;
        }
        break;
    }
    if (expect_lit(text, size, &pos, " kb=") || parse_u64_exact(text, size, &pos, &val) ||
        val >= NBT_CCS_SLOTS) {
        return -1;
    }
    tmp.knob_back_slot = (uint8_t)val;
    if (expect_lit(text, size, &pos, " cb=") || parse_u64_exact(text, size, &pos, &val) ||
        val >= NBT_CCS_SLOTS) {
        return -1;
    }
    tmp.ccs_back_slot = (uint8_t)val;
    if (expect_lit(text, size, &pos, " wd=") || parse_u64_exact(text, size, &pos, &val) ||
        val < NBT_CAPTURE_MIN_WATCHDOG_MS || val > NBT_CAPTURE_MAX_WATCHDOG_MS) {
        return -1;
    }
    tmp.watchdog_ms = (uint32_t)val;
    if (expect_lit(text, size, &pos, " esc=") || parse_u64_exact(text, size, &pos, &val) ||
        val > 1u) {
        return -1;
    }
    tmp.escape_enabled = (uint8_t)val;
    if (expect_lit(text, size, &pos, "\n") || pos != size || !nbt_capture_control_valid(&tmp)) {
        return -1;
    }
    *out = tmp;
    return 0;
}

int nbt_capture_open_shmem_checked(const char *path, int writing) {
    struct stat st, named;
    int flags = (writing ? (O_WRONLY | O_CREAT) : O_RDONLY) | O_NONBLOCK;
    int fd, valid, one_name;
    mode_t forbidden;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(path, flags, 0600);
    if (fd < 0) {
        return -1;
    }
    valid = !fcntl(fd, F_SETFD, FD_CLOEXEC) && !fstat(fd, &st) && !lstat(path, &named);
    one_name = valid && st.st_nlink == 1;
#ifdef __QNXNTO__
    if (valid && !strncmp(path, "/dev/shmem/", 11)) {
        one_name = st.st_nlink >= 1 && st.st_nlink <= 3;
    }
#endif
    forbidden = writing ? (mode_t)0022 : (mode_t)0002;
    valid = valid && S_ISREG(st.st_mode) && S_ISREG(named.st_mode) && st.st_uid == geteuid() &&
            one_name && !(st.st_mode & forbidden) && st.st_dev == named.st_dev &&
            st.st_ino == named.st_ino;
    if (!valid) {
        close(fd);
        errno = EPERM;
        return -1;
    }
    return fd;
}

int nbt_capture_control_write(const char *path, const nbt_capture_control *ctrl) {
    char buf[NBT_CAPTURE_CONTROL_MAX];
    const int len = nbt_capture_control_format(ctrl, buf, sizeof(buf));
    int fd, res;
    ssize_t count;
    if (len <= 0) {
        errno = EINVAL;
        return -1;
    }
    fd = nbt_capture_open_shmem_checked(path, 1);
    if (fd < 0) {
        return -1;
    }
    do {
        count = write(fd, buf, (size_t)len);
    } while (count < 0 && errno == EINTR);
    res = count == (ssize_t)len && !ftruncate(fd, (off_t)len) ? 0 : -1;
    if (close(fd)) {
        res = -1;
    }
    return res;
}

static int read_small_shmem(const char *path, char *buf, size_t cap, size_t *out_len) {
    const int fd = nbt_capture_open_shmem_checked(path, 0);
    size_t used = 0;
    if (fd < 0) {
        return -1;
    }
    for (;;) {
        const ssize_t n = read(fd, buf + used, cap - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
        if (used == cap) {
            close(fd);
            errno = EFBIG;
            return -1;
        }
    }
    if (close(fd)) {
        return -1;
    }
    *out_len = used;
    return 0;
}

int nbt_capture_control_read(const char *path, nbt_capture_control *out) {
    char buf[NBT_CAPTURE_CONTROL_MAX + 1u];
    size_t len = 0;
    if (!out || read_small_shmem(path, buf, sizeof(buf), &len)) {
        return -1;
    }
    return nbt_capture_control_parse(buf, len, out);
}

int nbt_capture_liveness_read(const char *path, pid_t expected_pid, uint64_t *out_ts_ms) {
    char buf[65];
    size_t len = 0, pos = 0;
    uint64_t pid = 0, ts = 0;
    if (!out_ts_ms || expected_pid <= 0 || read_small_shmem(path, buf, sizeof(buf), &len)) {
        return -1;
    }
    if (len == 0 || memchr(buf, '\0', len) != NULL || parse_u64_exact(buf, len, &pos, &pid) ||
        pid != (uint64_t)expected_pid || expect_lit(buf, len, &pos, " ") ||
        parse_u64_exact(buf, len, &pos, &ts) || expect_lit(buf, len, &pos, "\n") || pos != len) {
        return -1;
    }
    *out_ts_ms = ts;
    return 0;
}
