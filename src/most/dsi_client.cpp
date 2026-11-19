// SPDX-License-Identifier: MPL-2.0
#include "nbt/most/dsi_client.hpp"

#include "nbt/log.h"
#include "nbt/most.h"

#include <dlfcn.h>
#include <new>
#include <pthread.h>
#include <string.h>
#include <type_traits>

namespace {

using nbt::most::DsiClient;
using nbt::platform::dsi_abi::kClientBytes;
using nbt::platform::dsi_abi::kClientChannel;
using nbt::platform::dsi_abi::kClientIpcFd;
using nbt::platform::dsi_abi::kClientParty;
using nbt::platform::dsi_abi::kClientSocket;
using nbt::platform::dsi_abi::kConnectionBytes;
using nbt::platform::dsi_abi::kInputStreamId;
using nbt::platform::dsi_abi::kInputStreamLength;
using nbt::platform::dsi_abi::kInputStreamPayload;
using nbt::platform::dsi_abi::kServerParty;
using nbt::platform::dsi_abi::kSlotAttached;
using nbt::platform::dsi_abi::kSlotData;
using nbt::platform::dsi_abi::kSlotDetached;
using nbt::platform::dsi_abi::kStreamBytes;
using nbt::platform::dsi_abi::kVtableSlots;

const unsigned kRequestType = 0x100;
const unsigned kRequestCommand = 7;
const char *const kDefaultLibraries[] = {"/opt/sys/lib/libsys_dsi_colibry.gpp.so.8",
                                         "/fs/sda0/opt/sys/lib/libsys_dsi_colibry.gpp.so.8", NULL};

nbt::core::StaticMutex startLock = NBT_STATIC_MUTEX_INITIALIZER;
std::aligned_storage<sizeof(DsiClient), alignof(DsiClient)>::type clientStorage;
DsiClient *clientInstance = NULL;

extern "C" {

void dsiAttached(void *self) {
    DsiClient *client = DsiClient::shared();
    if (client) {
        client->onAttached(self);
    }
}

void dsiDetached(void *self) {
    DsiClient *client = DsiClient::shared();
    if (client) {
        client->onDetached(self);
    }
}

void dsiData(void *self, void *stream) {
    DsiClient *client = DsiClient::shared();
    if (client) {
        client->onData(self, stream);
    }
}

} // extern "C"

} // namespace

namespace nbt {
namespace most {

DsiClient::DsiClient()
    : sendLock_(), connectionCtor_(NULL), connectionAdd_(NULL), connectionStart_(NULL),
      clientCtor_(NULL), notifyFn_(NULL), streamCtor_(NULL), streamWrite_(NULL), streamSend_(NULL),
      streamDtor_(NULL), vtable_(NULL), connection_(), client_(), slots_(), handlers_(),
      library_(NULL), started_(false), attached_(false) {
    memset(&handlers_, 0, sizeof(handlers_));
}

DsiClient *DsiClient::shared() {
    return clientInstance;
}

bool DsiClient::load(const char *path) {
    for (size_t i = 0; !library_ && (path ? i == 0 : kDefaultLibraries[i] != NULL); ++i) {
        library_ = dlopen(path ? path : kDefaultLibraries[i], RTLD_NOW | RTLD_LOCAL);
    }
    if (!library_) {
        nbt_logf("DSI library not loaded");
        return false;
    }
    return true;
}

bool DsiClient::resolveSymbols() {
    connectionCtor_ = (ConnectionCtor)dlsym(library_, "_ZN14CDSIConnectionC1Ev");
    connectionAdd_ = (ConnectionAdd)dlsym(library_, "_ZN14CDSIConnection3addER10CDSIClient");
    connectionStart_ = (ConnectionStart)dlsym(library_, "_ZN14CDSIConnection5startEv");
    clientCtor_ = (ClientCtor)dlsym(library_, "_ZN10CDSIClientC1EPKcS1_ii");
    notifyFn_ = (ClientNotify)dlsym(library_, "_ZN10CDSIClient15setNotificationEj");
    streamCtor_ = (StreamCtor)dlsym(library_, "_ZN11CDSIOStreamC1ER11CDSIChannelP13CDSITCPSocketiN3"
                                              "DSI11RequestTypeENS4_7CommandEjRK8SPartyIDS9_i");
    streamWrite_ = (StreamWrite)dlsym(library_, "_ZN11CDSIOStream5writeEPKvj");
    streamSend_ = (StreamSend)dlsym(library_, "_ZN11CDSIOStream4sendEi");
    streamDtor_ = (StreamDtor)dlsym(library_, "_ZN11CDSIOStreamD1Ev");
    vtable_ = dlsym(library_, "_ZTV10CDSIClient");
    if (!connectionCtor_ || !connectionAdd_ || !connectionStart_ || !clientCtor_ || !notifyFn_ ||
        !streamCtor_ || !streamWrite_ || !streamSend_ || !streamDtor_ || !vtable_) {
        nbt_logf("DSI library symbols missing");
        return false;
    }
    return true;
}

int DsiClient::start(const char *path, const nbt_dsi_handlers *handlers) {
    if (started_ || !handlers) {
        return -1;
    }
    if (!load(path) || !resolveSymbols()) {
        return -1;
    }
    connection_.reset(new (std::nothrow) unsigned char[kConnectionBytes]);
    client_.reset(new (std::nothrow) unsigned char[kClientBytes]);
    if (!connection_ || !client_) {
        connection_.reset();
        client_.reset();
        return -1;
    }
    memset(connection_.get(), 0, kConnectionBytes);
    memset(client_.get(), 0, kClientBytes);
    handlers_ = *handlers;
    connectionCtor_(connection_.get());
    clientCtor_(client_.get(), NBT_MOST_INTERFACE, NBT_MOST_ROLE, NBT_MOST_MAJOR, NBT_MOST_MINOR);
    memcpy(slots_, (unsigned char *)vtable_ + 2 * sizeof(void *), sizeof(slots_));
    slots_[kSlotAttached] = (void *)(uintptr_t)&dsiAttached;
    slots_[kSlotDetached] = (void *)(uintptr_t)&dsiDetached;
    slots_[kSlotData] = (void *)(uintptr_t)&dsiData;
    void **table = slots_;
    memcpy(client_.get(), &table, sizeof(table));

    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes)) {
        connection_.reset();
        client_.reset();
        return -1;
    }
    if (pthread_attr_setstacksize(&attributes, 128 * 1024)) {
        pthread_attr_destroy(&attributes);
        connection_.reset();
        client_.reset();
        return -1;
    }
    (void)connectionAdd_(connection_.get(), client_.get());
    pthread_t thread;
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED) ||
        pthread_create(&thread, &attributes, run, NULL)) {
        pthread_attr_destroy(&attributes);
        started_ = true;
        (void)connection_.release();
        (void)client_.release();
        return -1;
    }
    pthread_attr_destroy(&attributes);
    started_ = true;
    return 0;
}

int DsiClient::request(unsigned requestId, const unsigned char *payload, size_t length) {
    uint64_t storage[kStreamBytes / 8 + 1];
    int result = -1;
    if ((length && !payload) || length > NBT_DSI_PAYLOAD_MAX) {
        return -1;
    }
    core::LockGuard lock(sendLock_);
    if (attached_ && client_) {
        int fd;
        memset(storage, 0, sizeof(storage));
        memcpy(&fd, client_.get() + kClientIpcFd, sizeof(fd));
        streamCtor_(storage, client_.get() + kClientChannel, client_.get() + kClientSocket, fd,
                    kRequestType, kRequestCommand, requestId, client_.get() + kClientParty,
                    client_.get() + kServerParty, 0);
        if (length) {
            streamWrite_(storage, payload, (unsigned)length);
        }
        result = streamSend_(storage, -1) == 0 ? 0 : -1;
        streamDtor_(storage);
    }
    return result;
}

int DsiClient::notify(uint32_t attribute) {
    core::LockGuard lock(sendLock_);
    if (!attached_ || !client_) {
        return -1;
    }
    notifyFn_(client_.get(), attribute);
    return 0;
}

void *DsiClient::run(void *unused) {
    (void)unused;
    DsiClient *client = shared();
    if (client && client->connectionStart_ && client->connection_) {
        client->connectionStart_(client->connection_.get());
    }
    return NULL;
}

void DsiClient::onAttached(void *self) {
    if (self != client_.get()) {
        return;
    }
    {
        core::LockGuard lock(sendLock_);
        attached_ = true;
    }
    if (handlers_.attached) {
        handlers_.attached(handlers_.user);
    }
}

void DsiClient::onDetached(void *self) {
    if (self != client_.get()) {
        return;
    }
    {
        core::LockGuard lock(sendLock_);
        attached_ = false;
    }
    if (handlers_.detached) {
        handlers_.detached(handlers_.user);
    }
}

void DsiClient::onData(void *self, void *stream) {
    const unsigned char *data;
    uint32_t id, length;
    if (self != client_.get() || !stream) {
        return;
    }
    memcpy(&id, (const unsigned char *)stream + kInputStreamId, sizeof(id));
    memcpy(&length, (const unsigned char *)stream + kInputStreamLength, sizeof(length));
    memcpy(&data, (const unsigned char *)stream + kInputStreamPayload, sizeof(data));
    if (!data || length > NBT_DSI_PAYLOAD_MAX) {
        return;
    }
    if (handlers_.data) {
        handlers_.data(id, data, length, handlers_.user);
    }
}

} // namespace most
} // namespace nbt

extern "C" {

int nbt_dsi_start(const char *path, const nbt_dsi_handlers *handlers) {
    nbt::core::LockGuard lock(startLock);
    if (!clientInstance) {
        clientInstance = new (&clientStorage) nbt::most::DsiClient();
    }
    return clientInstance->start(path, handlers);
}

int nbt_dsi_request(unsigned request, const unsigned char *payload, size_t length) {
    return clientInstance ? clientInstance->request(request, payload, length) : -1;
}

int nbt_dsi_notify(uint32_t attribute) {
    return clientInstance ? clientInstance->notify(attribute) : -1;
}

} // extern "C"
