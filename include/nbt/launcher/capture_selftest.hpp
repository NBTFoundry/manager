// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_CAPTURE_SELFTEST_HPP
#define NBT_LAUNCHER_CAPTURE_SELFTEST_HPP

#include <stddef.h>

namespace nbt {
namespace launcher {

/* Loads `shim` and drives it through its self-test entry with files in `dir`:
 * capture off, on (knob, BACK, another client), heartbeat stopped, switched
 * off, the 5 s BACK escape and a GUI that is gone. 0 when every step forwards
 * or holds as the build (observer or active) must, with the build in
 * *observer; otherwise -1 and the failing step in detail. Run it in a child
 * process: a faulty shim may crash it. */
int captureSelfTest(const char *shim, const char *dir, char *detail, size_t size, bool *observer);

} // namespace launcher
} // namespace nbt

#endif
