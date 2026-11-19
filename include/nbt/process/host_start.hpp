#ifndef NBT_PROCESS_HOST_START_HPP
#define NBT_PROCESS_HOST_START_HPP

/* What the application host prepares before starting an installed
 * application, once it holds the application's locks: the transaction
 * recovery (under the exclusive lease), the installed record, the MOST
 * channel, the environment and the bridge grant. */

#include "nbt/app_settings.h"
#include "nbt/fs.h"
#include "nbt/process/host_channels.hpp"
#include "nbt/transaction.h"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace process {

struct HostStart {
    nbt_installed record;
    nbt_app_environment environment;
    bool canShow;
    bool canStatus;
    uint64_t nowMs;
};

/* Closes `lease` once recovery ran. 0, or -1 with `reason` for the logs and
 * *error: the failed call's errno, or 0 when no system call failed (a refusal,
 * or a check that sets none), so no stale value passes for the cause. */
int prepareHostStart(nbt_fs *fs, const char *app, int lease, HostStart *out, char *reason,
                     size_t size, int *error, HostChannels &channels);

} // namespace process
} // namespace nbt

#endif
