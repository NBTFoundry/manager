#include "nbt/core/unique_fd.hpp"
#include "nbt/dsi.h"
#include "nbt/log.h"
#include "nbt/most/service.hpp"
#include "nbt/most_service.h"
#include "nbt/util.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>

namespace nbt {
namespace most {
namespace {
const int kBufferBytes = 64 * 1024;
} // namespace

int MostService::socket(int *host, int *app) {
    if (!host || !app) {
        return -1;
    }
    int fds[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fds)) {
        return -1;
    }
    core::UniqueFd hostFd(fds[0]);
    core::UniqueFd appFd(fds[1]);
    for (int i = 0; i < 2; ++i) {
        if (setsockopt(fds[i], SOL_SOCKET, SO_SNDBUF, &kBufferBytes, sizeof(kBufferBytes)) ||
            setsockopt(fds[i], SOL_SOCKET, SO_RCVBUF, &kBufferBytes, sizeof(kBufferBytes)) ||
            fcntl(fds[i], F_SETFD, FD_CLOEXEC)) {
            return -1;
        }
    }
    if (fcntl(hostFd.get(), F_SETFL, O_NONBLOCK)) {
        return -1;
    }
    *host = hostFd.release();
    *app = appFd.release();
    return 0;
}

} // namespace most
} // namespace nbt

extern "C" {
int nbt_most_service_socket(int *host, int *app) {
    return nbt::most::MostService::socket(host, app);
}

int nbt_most_service_poll(uint64_t now) {
    nbt::most::MostService *service = nbt::most::MostService::shared();
    return service ? service->poll(now) : -1;
}

int nbt_most_service_fd(void) {
    nbt::most::MostService *service = nbt::most::MostService::shared();
    return service ? service->fd() : -1;
}

void nbt_most_service_pause(int paused) {
    nbt::most::MostService *service = nbt::most::MostService::shared();
    if (service) {
        service->setPaused(paused != 0);
    }
}

void nbt_most_service_controller_shown(int shown) {
    nbt::most::MostService *service = nbt::most::MostService::shared();
    if (service) {
        service->setShown(shown != 0);
    }
}

void nbt_most_service_stop(void) {
    nbt::most::MostService *service = nbt::most::MostService::shared();
    uint64_t now = 0;
    if (!service || service->fd() < 0 || nbt_monotonic_ms(&now)) {
        return;
    }
    const uint64_t deadline = now + 400;
    for (;;) {
        const unsigned char *payload = NULL;
        const long n = service->prepareClear(now, &payload);
        if (!n) {
            return;
        }
        if (n > 0 && nbt_dsi_request(NBT_MOST_REQ_PROCESS_MESSAGE, payload, (size_t)n)) {
            nbt_logf("MOST: notification clear send failed; relying on server cleanup");
        }
        if (n < 0) {
            (void)poll(NULL, 0, 10);
        }
        if (nbt_monotonic_ms(&now) || now >= deadline) {
            nbt_logf("MOST: notification clear deadline; relying on server cleanup");
            return;
        }
    }
}
}
