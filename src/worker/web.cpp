// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/web.hpp"

#include "nbt/firewall.h"
#include "nbt/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace nbt {
namespace worker {
namespace {

core::Status privateFd(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        return core::Status::fromErrno("cannot configure connection");
    }
    return core::Status();
}

bool allowedInterface(const char *interfaceName) {
    if (!interfaceName) {
        return false;
    }
    if (!strcmp(interfaceName, "uap0") || !strcmp(interfaceName, "wfd0")) {
        return true;
    }
#ifdef NBT_TESTING
    if (!strcmp(interfaceName, "lo")) {
        return true;
    }
#endif
    return false;
}

} // namespace

WebServer::WebServer(Worker &worker)
    : api_(worker), listener_(), firewall_(false), interface_(), address_(0), port_(0),
      firewallMs_(0), host_() {
    host_[0] = 0;
}

core::Status WebServer::listen(const char *interfaceName, unsigned port) {
    if (listener_.valid() || port > 65535 || !allowedInterface(interfaceName)) {
        return core::Status::error(EINVAL, "invalid web listener");
    }
    core::UniqueFd fd(socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.valid()) {
        return core::Status::fromErrno("cannot create web listener");
    }
    struct ifreq iface;
    memset(&iface, 0, sizeof(iface));
    if (strlen(interfaceName) >= sizeof(iface.ifr_name)) {
        return core::Status::error(EINVAL, "interface name too long");
    }
    memcpy(iface.ifr_name, interfaceName, strlen(interfaceName) + 1);
    if (ioctl(fd.get(), SIOCGIFADDR, &iface) || iface.ifr_addr.sa_family != AF_INET) {
        return core::Status::fromErrno("cannot inspect interface");
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr = reinterpret_cast<struct sockaddr_in *>(&iface.ifr_addr)->sin_addr;
    address.sin_port = htons(static_cast<uint16_t>(port));
    char ip[INET_ADDRSTRLEN];
    int yes = 1;
    socklen_t addressLength = sizeof(address);
    if (address.sin_addr.s_addr == htonl(INADDR_ANY) ||
        !inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip)) ||
        setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) ||
        bind(fd.get(), reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) ||
        ::listen(fd.get(), static_cast<int>(kWebClients)) ||
        getsockname(fd.get(), reinterpret_cast<struct sockaddr *>(&address), &addressLength) ||
        !privateFd(fd.get()).ok() || fd.get() >= FD_SETSIZE) {
        return core::Status::fromErrno("cannot bind web listener");
    }
    const int hostLength =
        snprintf(host_, sizeof(host_), "%s:%u", ip, static_cast<unsigned>(ntohs(address.sin_port)));
    if (hostLength < 0 || hostLength >= static_cast<int>(sizeof(host_))) {
        return core::Status::error(ERANGE, "web address too long");
    }
    listener_ = std::move(fd);
    api_.address(host_, interfaceName);
    snprintf(interface_, sizeof(interface_), "%s", interfaceName);
    address_ = address.sin_addr.s_addr;
    port_ = ntohs(address.sin_port);
    firewallMs_ = 0;
    if (nbt_firewall_open_label(interfaceName, address_, port_,
                                api_.door() ? "nbt-door" : "nbt-manager")) {
        nbt_logf("firewall: cannot open TCP %u on %s (errno %d); phones may not reach the API",
                 static_cast<unsigned>(ntohs(address.sin_port)), interfaceName, errno);
    } else {
        firewall_ = true;
        nbt_logf("firewall: TCP %u open on %s for this session",
                 static_cast<unsigned>(ntohs(address.sin_port)), interfaceName);
    }
    return core::Status();
}

void WebServer::dropClients(Worker &worker) {
    (void)worker;
    for (unsigned i = 0; i < kWebClients; ++i) {
        if (clients_[i].valid()) {
            clients_[i].closeClient(api_);
        }
    }
}

void WebServer::close(Worker &worker) {
    listener_.reset();
    if (firewall_ || port_) {
        firewall_ = false;
        if (nbt_firewall_close_label(api_.door() ? "nbt-door" : "nbt-manager") < 0) {
            nbt_logf("firewall: cannot remove the session rule (errno %d)", errno);
        } else {
            nbt_logf("firewall: session rule removed");
        }
    }
    dropClients(worker);
    host_[0] = 0;
    port_ = 0;
}

void WebServer::sets(fd_set *readers, fd_set *writers, int *highest) {
    if (listener_.valid()) {
        FD_SET(listener_.get(), readers);
        if (listener_.get() > *highest) {
            *highest = listener_.get();
        }
    }
    for (unsigned i = 0; i < kWebClients; ++i) {
        WebConnection &connection = clients_[i];
        if (!connection.valid()) {
            continue;
        }
        if (connection.phase() == WebConnection::Phase::Wait) {
            continue; // polled each round in handle()
        }
        if (connection.phase() == WebConnection::Phase::Reply) {
            FD_SET(connection.fd(), writers);
        } else {
            FD_SET(connection.fd(), readers);
        }
        if (connection.fd() > *highest) {
            *highest = connection.fd();
        }
    }
}

void WebServer::handle(Worker &worker, const fd_set *readers, const fd_set *writers,
                       uint64_t nowMs) {
    (void)worker;
    if (listener_.valid() && api_.door() && (nowMs < firewallMs_ || nowMs - firewallMs_ >= 10000)) {
        if (!nbt_firewall_open_label(interface_, address_, port_, "nbt-door")) {
            firewall_ = true;
        }
        firewallMs_ = nowMs;
    }
    if (listener_.valid() && FD_ISSET(listener_.get(), readers)) {
        core::UniqueFd fd(accept(listener_.get(), NULL, NULL));
        if (fd.valid()) {
            unsigned i = 0;
            while (i < kWebClients && clients_[i].valid()) {
                ++i;
            }
            if (i < kWebClients && fd.get() < FD_SETSIZE && privateFd(fd.get()).ok()) {
                clients_[i].acceptClient(std::move(fd), nowMs);
            }
        }
    }
    for (unsigned i = 0; i < kWebClients; ++i) {
        WebConnection &connection = clients_[i];
        if (!connection.valid()) {
            continue;
        }
        if (nowMs < connection.lastMs() || nowMs - connection.lastMs() > kConnectionTimeoutMs) {
            connection.closeClient(api_);
            continue;
        }
        if (connection.phase() == WebConnection::Phase::Wait) {
            if (!connection.poll(api_, nowMs).ok()) {
                connection.closeClient(api_);
            }
            continue;
        }
        if (FD_ISSET(connection.fd(), readers) && !connection.readClient(host_, api_, nowMs).ok()) {
            connection.closeClient(api_);
            continue;
        }
        if (connection.valid() && connection.phase() == WebConnection::Phase::Reply &&
            FD_ISSET(connection.fd(), writers)) {
            bool completed = false;
            if (!connection.writeReply(nowMs, &completed) || completed) {
                connection.closeClient(api_);
            }
        }
    }
}

} // namespace worker
} // namespace nbt
