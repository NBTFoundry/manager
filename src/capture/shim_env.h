// SPDX-License-Identifier: MPL-2.0
/* Internal to the capture shim (hidden symbols in libnbt-capture.so). */
#ifndef NBT_CAPTURE_SHIM_ENV_H
#define NBT_CAPTURE_SHIM_ENV_H

#include <stdint.h>

/* Redirects the paths and clock below to `dir` and `clock` for the install
 * self-test; once only. The caller refuses it inside HmiMain3D. */
int nbt_shim_selftest_set(const char *dir, uint64_t (*clock)(void));

const char *nbt_shim_control_path(void);
const char *nbt_shim_liveness_path(void);
const char *nbt_shim_diag_path(void);
uint64_t nbt_shim_monotonic_ms(void);
/* 1 when this process is HmiMain3D (tests: NBT_TEST_CAPTURE_EXE). */
int nbt_shim_is_hmi_process(void);
/* iDrive's environment file (tests: NBT_TEST_SHIM_HMI_ENV). */
const char *nbt_shim_hmi_env_path(void);

#endif
