// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_HOST_SIGNALS_HPP
#define NBT_PROCESS_HOST_SIGNALS_HPP

/* An application host's signals: SIGTERM and SIGINT stop it, SIGUSR1 asks its
 * application to the front (show), SIGUSR2 is the GUI's pause (iDrive took the
 * screen); SIGHUP and SIGPIPE are ignored. The handlers only set flags. */

namespace nbt {
namespace process {

int installHostSignals();
bool hostStopping();
void stopHost();
bool showPending();
void showDone();
/* Whether a pause was asked since the last call. */
bool takePause();

} // namespace process
} // namespace nbt

#endif
