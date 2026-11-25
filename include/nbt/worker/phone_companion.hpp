// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_PHONE_COMPANION_HPP
#define NBT_WORKER_PHONE_COMPANION_HPP
#include "nbt/core/span.hpp"
namespace nbt {
namespace worker {
class Worker;
/* /v1/companion: "app\n" gives "COMPANION\tready\n" and the page, or
 * "COMPANION\toff\n" while its process does not run or has no page; 404 for an
 * application that is not installed or declares no `companion`
 * (docs/companion-bridge.md). */
int phoneCompanion(Worker &worker, const char *body, core::Span<char> output);
/* /v1/companion/event: "app\tid\tvalue\n" adds the event; 409 while its
 * process does not run. */
int phoneCompanionEvent(Worker &worker, const char *body, core::Span<char> output);
} // namespace worker
} // namespace nbt
#endif
