// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_TARGET_H
#define NBT_TARGET_H
/* A target must positively identify QNX 6.5 and an ARMv7 processor. */
int nbt_target_compatible(const char *system, const char *release, int armv7);
#endif
