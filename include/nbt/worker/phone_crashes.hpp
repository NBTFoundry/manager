// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_PHONE_CRASHES_HPP
#define NBT_WORKER_PHONE_CRASHES_HPP

/* The crash reports a paired phone lists, reads and clears (PROTOCOL.md,
 * "Crash reports"; nbt/log/crash_report.hpp). `app` in a body is an installed
 * application or "manager"; an empty body means all of them. */

#include "nbt/core/span.hpp"
#include "nbt/storage/managed_root.hpp"
#include "nbt/worker/phone_logs.hpp"

namespace nbt {
namespace worker {

/* The newest reports a listing of all applications names. */
static const unsigned kCrashListMax = 16;

/* POST /v1/crashes, body "" or "<app>\n": "CRASHES\t<n>\n", then a line per
 * report, newest first: "<app>\t<slot>\t<report>\t<time>\t<version>\t<ended>".
 * 400 for a malformed body or an unknown application, 503 when a report
 * cannot be read. */
int phoneCrashList(storage::ManagedRoot &root, const char *body, core::Span<char> out);
/* POST /v1/crashes/read, body "<app>\t<slot>\n": the report, streamed (200);
 * 409 when that slot holds none. */
int phoneCrashRead(storage::ManagedRoot &root, const char *body, LogReply &reply);
/* POST /v1/crashes/clear, body "" or "<app>\n": removes the reports. */
int phoneCrashClear(storage::ManagedRoot &root, const char *body, core::Span<char> out);

} // namespace worker
} // namespace nbt

#endif
