/* The application on the panel when iDrive takes the screen (NAV, MENU,
 * OPTION, a call): the GUI asks its host to pause it (SIGUSR2). The host
 * stops it, continues it on the next show and stops it after 2 minutes
 * (nbt/process/app_pause.hpp). */
#include "nbt/app_host.h"
#include "nbt/display/layer_manager.hpp"
#include "nbt/fs.h"
#include "nbt/gui/gui_session.hpp"
#include "nbt/log.h"

#include <errno.h>
#include <signal.h>

namespace nbt {
namespace gui {

void GuiSession::pauseShownApp() {
    if (!panel_.active(display::kAppSurface) || !ui_.target[0]) {
        return;
    }
    nbt_fs fs;
    char error[128];
    if (!config_.root || nbt_fs_init(&fs, config_.root, error, sizeof(error))) {
        nbt_logf("cannot pause %s: no managed root", ui_.target);
        return;
    }
    errno = 0;
    if (nbt_app_host_signal(&fs, ui_.target, SIGUSR2)) {
        nbt_logf("cannot pause %s (errno %d)", ui_.target, errno);
        return;
    }
    nbt_logf("%s paused while iDrive is shown; stopped after 2 minutes", ui_.target);
}

} // namespace gui
} // namespace nbt
