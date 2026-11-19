/* The capture shim's environment: its file paths, clock and process check,
 * and the install self-test's redirection of them (src/launcher/
 * capture_selftest.cpp). Inside HmiMain3D the self-test is refused, so these
 * are always iDrive's /dev/shmem files and the monotonic clock there. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "shim_env.h"

#include "nbt/capture/protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __QNXNTO__
#include <devctl.h>
#include <sys/procfs.h>
#endif

static int g_selftest = 0;
static char g_selftest_control[256], g_selftest_live[256], g_selftest_diag[256];
static uint64_t (*g_selftest_clock)(void) = NULL;

int nbt_shim_selftest_set(const char *dir, uint64_t (*clock)(void)) {
    if (g_selftest || !dir || !clock ||
        snprintf(g_selftest_control, sizeof(g_selftest_control), "%s/capture-selftest.control",
                 dir) >= (int)sizeof(g_selftest_control) ||
        snprintf(g_selftest_live, sizeof(g_selftest_live), "%s/capture-selftest.live", dir) >=
            (int)sizeof(g_selftest_live) ||
        snprintf(g_selftest_diag, sizeof(g_selftest_diag), "%s/capture-selftest.diag", dir) >=
            (int)sizeof(g_selftest_diag)) {
        return -1;
    }
    g_selftest_clock = clock;
    g_selftest = 1;
    return 0;
}

const char *nbt_shim_control_path(void) {
    if (g_selftest) {
        return g_selftest_control;
    }
#ifdef NBT_TESTING
    const char *p = getenv("NBT_TEST_CAPTURE_CONTROL");
    if (p && p[0]) {
        return p;
    }
#endif
    return NBT_CAPTURE_CONTROL_PATH;
}

const char *nbt_shim_liveness_path(void) {
    if (g_selftest) {
        return g_selftest_live;
    }
#ifdef NBT_TESTING
    const char *p = getenv("NBT_TEST_GUI_LIVENESS");
    if (p && p[0]) {
        return p;
    }
#endif
    return NBT_CAPTURE_LIVENESS_PATH;
}

const char *nbt_shim_diag_path(void) {
    if (g_selftest) {
        return g_selftest_diag;
    }
#ifdef NBT_TESTING
    const char *p = getenv("NBT_TEST_CAPTURE_DIAG");
    if (p && p[0]) {
        return p;
    }
#endif
    return NBT_CAPTURE_DIAG_PATH;
}

uint64_t nbt_shim_monotonic_ms(void) {
    struct timespec ts;
    if (g_selftest && g_selftest_clock) {
        return g_selftest_clock();
    }
#ifdef NBT_TESTING
    const char *clock_file = getenv("NBT_TEST_CAPTURE_CLOCK_FILE");
    if (clock_file && clock_file[0]) {
        int fd = open(clock_file, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            char buf[32];
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                uint64_t v = 0;
                ssize_t i;
                for (i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; ++i) {
                    v = v * 10u + (uint64_t)(buf[i] - '0');
                }
                if (i > 0) {
                    return v;
                }
            }
        }
    }
#endif
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

const char *nbt_shim_hmi_env_path(void) {
#ifdef NBT_TESTING
    const char *p = getenv("NBT_TEST_SHIM_HMI_ENV");
    if (p && p[0]) {
        return p;
    }
#endif
    return NBT_CAPTURE_HMI_ENV_PATH;
}

/* Loaded into iDrive through its environment file: delete the file before
 * iDrive's main, so if this start fails the next one is stock. The manager
 * arms it again once iDrive has run 60 s with the shim. */
__attribute__((constructor)) static void consume_hmi_env(void) {
    if (nbt_shim_is_hmi_process()) {
        (void)unlink(nbt_shim_hmi_env_path());
    }
}

int nbt_shim_is_hmi_process(void) {
    const char *expected = "HmiMain3D";
    char path[256];
    const char *base;
#ifdef NBT_TESTING
    const char *override_exe = getenv("NBT_TEST_CAPTURE_EXE");
    if (override_exe && override_exe[0]) {
        expected = override_exe;
    }
#endif
    memset(path, 0, sizeof(path));
#ifdef __QNXNTO__
    {
        union {
            procfs_debuginfo info;
            char space[sizeof(procfs_debuginfo) + 256];
        } map;
        int fd = open("/proc/self/as", O_RDONLY);
        if (fd < 0) {
            return 0;
        }
        memset(&map, 0, sizeof(map));
        if (devctl(fd, DCMD_PROC_MAPDEBUG_BASE, &map.info, sizeof(map), NULL) != EOK) {
            close(fd);
            return 0;
        }
        close(fd);
        map.space[sizeof(map.space) - 1] = '\0';
        memcpy(path, map.info.path, sizeof(path) - 1);
    }
#else
    {
        ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (n <= 0) {
            return 0;
        }
        path[n] = '\0';
    }
#endif
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strlen(base) == strlen(expected) && memcmp(base, expected, strlen(expected)) == 0;
}
