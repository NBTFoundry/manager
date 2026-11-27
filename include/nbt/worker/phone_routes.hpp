// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_PHONE_ROUTES_HPP
#define NBT_WORKER_PHONE_ROUTES_HPP
#include "nbt/core/span.hpp"
#include "nbt/storage/developer_store.hpp"
namespace nbt {
namespace worker {
class Worker;
int phoneApplications(Worker &worker, core::Span<char> output);
int phoneSettings(Worker &worker, const char *body, bool setting, core::Span<char> output);
/* /v1/autostart: "app\n" reads Start with the car, "app\t<on|off>\n" sets it. */
int phoneAutostart(Worker &worker, const char *body, core::Span<char> output);
/* /v1/manager: the installed NBT Manager's version and sequence, and the
 * release formats a phone update can use now (PROTOCOL.md). */
int phoneManager(Worker &worker, core::Span<char> output);
/* /v1/theme: "THEME\t<bmw|mini|supra>\n", the theme the car's screen shows:
 * the Theme setting, or with Automatic the car's make (nbt/model/theme.hpp). */
int phoneTheme(Worker &worker, core::Span<char> output);
/* /v1/keys: the developer keys the car trusts, "KEYS\t<count>\n" then
 * "<key id>\t<name>\t<added>\n" each, newest first. */
int phoneDeveloperKeys(storage::DeveloperStore &developers, core::Span<char> output);
} // namespace worker
} // namespace nbt
#endif
