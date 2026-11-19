// SPDX-License-Identifier: MPL-2.0
#include "nbt/core/system.hpp"
#include "nbt/gui/phone_wake.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/util.h"
#include "nbt/worker/daemon.hpp"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
namespace nbt {
namespace worker {
namespace {
volatile sig_atomic_t doorStopping = 0;
void stopDoor(int) {
    doorStopping = 1;
}
} // namespace
int doorMain(const char *rootPath, int argc, char **argv) {
    const char *iface = "wfd0";
    unsigned port = 8081;
    int readyFd = -1;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 == argc) {
            return 2;
        }
        uint64_t number;
        if (!strcmp(argv[i], "--ready-fd")) {
            if (nbt_parse_u64(argv[i + 1], &number, 0) || number < 3 || number >= FD_SETSIZE) {
                return 2;
            }
            readyFd = static_cast<int>(number);
        }
#ifdef NBT_TESTING
        else if (!strcmp(argv[i], "--root"))
            rootPath = argv[i + 1];
        else if (!strcmp(argv[i], "--interface")) {
            if (strcmp(argv[i + 1], "lo")) {
                return 2;
            }
            iface = "lo";
        } else if (!strcmp(argv[i], "--port")) {
            if (nbt_parse_u64(argv[i + 1], &number, 0) || !number || number > 65535) {
                return 2;
            }
            port = static_cast<unsigned>(number);
        }
#endif
        else
            return 2;
    }
    storage::ManagedRoot root;
    if (!root.initialize(rootPath).ok()) {
        return 1;
    }
    char livePath[1024];
    snprintf(livePath, sizeof(livePath), "%s", gui::kDoorLivePath);
#ifdef NBT_TESTING
    if (strlen(rootPath) + 32 >= sizeof(livePath)) {
        return 1;
    }
    snprintf(livePath, sizeof(livePath), "%s/state/manager/door.live", rootPath);
#endif
    core::UniqueFd lifetime(
        storage::SafePathOpen::lockNonBlocking(root.legacy(), NBT_FS_DOOR_LOCK, "manager"));
    if (!lifetime.valid()) {
        return 1;
    }
    process::RealChildRunner children;
    process::RealHostControl hosts;
    Worker worker(root, children, hosts);
    WebServer web(worker);
    if (!web.initialize(true).ok()) {
        return 1;
    }
    gui::PhoneWake live;
    live.state = gui::WakeOpen;
    if (nbt_fs_random_tx(live.id)) {
        return 1;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stopDoor;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) {
        return 1;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL)) {
        return 1;
    }
    if (readyFd >= 0) {
        if (write(readyFd, "READY\n", 6) != 6 || close(readyFd)) {
            return 1;
        }
    }
    uint64_t nextListen = 0, nextLive = 0;
    while (!doorStopping) {
        core::Result<uint64_t> now = core::monotonicMs();
        if (!now.ok()) {
            return 1;
        }
        if (!web.listening() && now.value() >= nextListen) {
            (void)web.listen(iface, port);
            nextListen = now.value() + 1000;
        }
        if (now.value() >= nextLive) {
            char checked[NBT_PATH_MAX];
            if (!root.path(NBT_FS_PHONES_DIR, "manager", NULL,
                           core::Span<char>(checked, sizeof(checked)))
                     .ok()) {
                break;
            }
            live.when = now.value();
            if (!gui::phoneWakeWrite(livePath, live)) {
                return 1;
            }
            nextLive = now.value() + 1000;
        }
        fd_set readers, writers;
        FD_ZERO(&readers);
        FD_ZERO(&writers);
        int highest = -1;
        web.sets(&readers, &writers, &highest);
        struct timeval timeout = {0, 200000};
        const int selected = select(highest + 1, &readers, &writers, NULL, &timeout);
        if (selected < 0 && errno == EINTR) {
            continue;
        }
        if (selected < 0) {
            return 1;
        }
        now = core::monotonicMs();
        if (!now.ok()) {
            return 1;
        }
        web.handle(worker, &readers, &writers, now.value());
    }
    web.close(worker);
    return 0;
}
} // namespace worker
} // namespace nbt
