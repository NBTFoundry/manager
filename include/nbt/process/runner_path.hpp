// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_RUNNER_PATH_HPP
#define NBT_PROCESS_RUNNER_PATH_HPP

#include "nbt/bridge.h"

namespace nbt {
namespace process {

/* nbt-runner sits beside the worker in its release slot
 * (manager/slots/<n>/runner, docs/manager-release.md): the host and the worker
 * set it from their own absolute path. */
void setRunnerBeside(const char *self);
/* The runner to start an application built without the QNX SDP with, or NULL
 * when unknown. Test builds: NBT_TEST_RUNNER overrides it. */
const char *runnerPath();
/* Drops `runner` from what `provider` serves when no runner is there to start
 * a payload with: a slot staged from a release without one (a step release,
 * docs/manager-release.md) refuses such applications instead of failing them. */
void withoutMissingRunner(nbt_bridge_provider *provider);

} // namespace process
} // namespace nbt

#endif
