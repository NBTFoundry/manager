// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/web.hpp"

#include "nbt/firewall.h"
#include "nbt/log.h"
#include "nbt/worker/phone_network.hpp"

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

WebServer::WebServer(Worker &worker) : api_(worker), listeners_(), firewallMs_(0), via_() {
    for (unsigned i = 0; i < kWebListeners; ++i) {
        listeners_[i].firewall = false;
        listeners_[i].interface[0] = listeners_[i].host[0] = listeners_[i].bssid[0] = 0;
        listeners_[i].label[0] = 0;
        listeners_[i].address = 0;
        listeners_[i].port = 0;
    }
}

core::Status WebServer::listen(const char *interfaceName, unsigned port) {
    return listeners_[0].fd.valid() ? core::Status::error(EINVAL, "invalid web listener")
                                    : open(0, interfaceName, port);
}

core::Status WebServer::listenAlso(const char *interfaceName, unsigned port) {
    if (listeningOn(interfaceName)) {
        return core::Status::error(EINVAL, "invalid web listener");
    }
    for (unsigned i = 1; i < kWebListeners; ++i) {
        if (!listeners_[i].fd.valid()) {
            return open(i, interfaceName, port);
        }
    }
    return core::Status::error(ENOSPC, "no web listener left");
}

bool WebServer::listeningOn(const char *interfaceName) const {
    for (unsigned i = 0; i < kWebListeners; ++i) {
        if (listeners_[i].fd.valid() && !strcmp(listeners_[i].interface, interfaceName)) {
            return true;
        }
    }
    return false;
}

core::Status WebServer::open(unsigned index, const char *interfaceName, unsigned port) {
    if (port > 65535 || !allowedInterface(interfaceName)) {
        return core::Status::error(EINVAL, "invalid web listener");
    }
    Listener &l = listeners_[index];
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
    const int hostLength = snprintf(l.host, sizeof(l.host), "%s:%u", ip,
                                    static_cast<unsigned>(ntohs(address.sin_port)));
    if (hostLength < 0 || hostLength >= static_cast<int>(sizeof(l.host))) {
        l.host[0] = 0;
        return core::Status::error(ERANGE, "web address too long");
    }
    l.fd = std::move(fd);
    phoneBssid(interfaceName, l.bssid);
    snprintf(l.interface, sizeof(l.interface), "%s", interfaceName);
    // The first interface keeps the label of earlier versions; closing it removes both.
    snprintf(l.label, sizeof(l.label), index ? "%s-%s" : "%s",
             api_.door() ? "nbt-door" : "nbt-manager", interfaceName);
    l.address = address.sin_addr.s_addr;
    l.port = ntohs(address.sin_port);
    if (index) {
        nbt_logf("web: also listening on %s (%s)", interfaceName, l.host);
    }
    firewallMs_ = 0;
    openFirewall(l);
    return core::Status();
}

void WebServer::openFirewall(Listener &l) {
    if (nbt_firewall_open_label(l.interface, l.address, l.port, l.label)) {
        nbt_logf("firewall: cannot open TCP %u on %s (errno %d); phones may not reach the API",
                 static_cast<unsigned>(l.port), l.interface, errno);
    } else {
        if (!l.firewall) {
            nbt_logf("firewall: TCP %u open on %s for this session", static_cast<unsigned>(l.port),
                     l.interface);
        }
        l.firewall = true;
    }
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
    bool opened = false;
    for (unsigned i = 0; i < kWebListeners; ++i) {
        Listener &l = listeners_[i];
        opened = opened || l.firewall || l.port;
        l.fd.reset();
        l.firewall = false;
        l.host[0] = l.bssid[0] = l.interface[0] = 0;
        l.port = 0;
    }
    if (opened) {
        if (nbt_firewall_close_label(api_.door() ? "nbt-door" : "nbt-manager") < 0) {
            nbt_logf("firewall: cannot remove the session rule (errno %d)", errno);
        } else {
            nbt_logf("firewall: session rule removed");
        }
    }
    dropClients(worker);
}

void WebServer::sets(fd_set *readers, fd_set *writers, int *highest) {
    for (unsigned i = 0; i < kWebListeners; ++i) {
        if (listeners_[i].fd.valid()) {
            FD_SET(listeners_[i].fd.get(), readers);
            if (listeners_[i].fd.get() > *highest) {
                *highest = listeners_[i].fd.get();
            }
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

void WebServer::accept(unsigned index, uint64_t nowMs) {
    core::UniqueFd fd(::accept(listeners_[index].fd.get(), NULL, NULL));
    if (!fd.valid()) {
        return;
    }
    unsigned i = 0;
    while (i < kWebClients && clients_[i].valid()) {
        ++i;
    }
    if (i < kWebClients && fd.get() < FD_SETSIZE && privateFd(fd.get()).ok()) {
        clients_[i].acceptClient(std::move(fd), nowMs);
        via_[i] = index;
    }
}

void WebServer::handle(Worker &worker, const fd_set *readers, const fd_set *writers,
                       uint64_t nowMs) {
    (void)worker;
    // The door's rules again every 10 s: they go when the car reloads its ruleset.
    const bool refresh = api_.door() && (nowMs < firewallMs_ || nowMs - firewallMs_ >= 10000);
    for (unsigned i = 0; i < kWebListeners; ++i) {
        if (!listeners_[i].fd.valid()) {
            continue;
        }
        if (refresh && !nbt_firewall_open_label(listeners_[i].interface, listeners_[i].address,
                                                listeners_[i].port, listeners_[i].label)) {
            listeners_[i].firewall = true;
        }
        if (FD_ISSET(listeners_[i].fd.get(), readers)) {
            accept(i, nowMs);
        }
    }
    if (refresh) {
        firewallMs_ = nowMs;
    }
    for (unsigned i = 0; i < kWebClients; ++i) {
        WebConnection &connection = clients_[i];
        if (!connection.valid()) {
            continue;
        }
        // Pairing and status replies name the address and access point the phone came through.
        const Listener &via = listeners_[via_[i] < kWebListeners ? via_[i] : 0];
        api_.address(via.host, via.bssid);
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
        if (FD_ISSET(connection.fd(), readers) &&
            !connection.readClient(via.host, api_, nowMs).ok()) {
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
