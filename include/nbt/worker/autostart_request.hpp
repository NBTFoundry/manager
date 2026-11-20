// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_AUTOSTART_REQUEST_HPP
#define NBT_WORKER_AUTOSTART_REQUEST_HPP

#include "nbt/core/span.hpp"
#include "nbt/worker/worker.hpp"

namespace nbt {
namespace worker {

enum AutostartResult { AutostartDone, AutostartInvalid, AutostartRefused };

/* Start with the car, for the GUI (AUTOSTART) and paired phones
 * (/v1/autostart): PROTOCOL.md, "Autostart". With `mode` NULL, `out` gets
 * "AUTOSTART\t<off|on>\t<ready|blocked>\n"; with "on" or "off" the
 * preference is saved (the guard is untouched) and `out` gets "OK\n".
 * Invalid: not an installed application, the manager, or another mode.
 * Refused: the application is being updated, or its records cannot be read
 * or written. */
AutostartResult autostartRequest(Worker &worker, const char *app, const char *mode,
                                 core::Span<char> out);

} // namespace worker
} // namespace nbt

#endif
