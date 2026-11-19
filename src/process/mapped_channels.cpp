#include "nbt/process/mapped_channels.hpp"
#include <fcntl.h>
namespace nbt {
namespace process {
MappedChannels::MappedChannels(int status, int most, int gamepad, int controller, int output)
    : fds_(), valid_(true) {
    const int sources[] = {status, most, gamepad, controller, output};
    for (unsigned i = 0; i < 5; ++i) {
        if (sources[i] < 0) {
            continue;
        }
        fds_[i].reset(fcntl(sources[i], F_DUPFD, 10));
        if (!fds_[i].valid()) {
            valid_ = false;
            break;
        }
    }
}
} // namespace process
} // namespace nbt
