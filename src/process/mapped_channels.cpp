// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/mapped_channels.hpp"
#include <fcntl.h>
namespace nbt {
namespace process {
MappedChannels::MappedChannels(int status, int most, int gamepad, int controller, int output,
                               int hold, int diag)
    : fds_(), valid_(true) {
    const int sources[] = {status, most, gamepad, controller, output, hold, diag};
    for (unsigned i = 0; i < 7; ++i) {
        if (sources[i] < 0) {
            continue;
        }
        fds_[i].reset(fcntl(sources[i], F_DUPFD, 11));
        if (!fds_[i].valid()) {
            valid_ = false;
            break;
        }
    }
}
} // namespace process
} // namespace nbt
