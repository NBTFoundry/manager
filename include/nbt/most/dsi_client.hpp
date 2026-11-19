// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MOST_DSI_CLIENT_HPP
#define NBT_MOST_DSI_CLIENT_HPP

#include "nbt/core/mutex.hpp"
#include "nbt/dsi.h"
#include "nbt/platform/dsi_abi.hpp"

#include <memory>

namespace nbt {
namespace most {

class DsiClient {
public:
    DsiClient();

    int start(const char *library, const nbt_dsi_handlers *handlers);
    int request(unsigned request, const unsigned char *payload, size_t length);
    int notify(uint32_t attribute);

    static DsiClient *shared();
    void onAttached(void *self);
    void onDetached(void *self);
    void onData(void *self, void *stream);

private:
    typedef void (*ConnectionCtor)(void *);
    typedef int (*ConnectionAdd)(void *, void *);
    typedef void (*ConnectionStart)(void *);
    typedef void (*ClientCtor)(void *, const char *, const char *, int, int);
    typedef void (*ClientNotify)(void *, unsigned);
    typedef void (*StreamCtor)(void *, void *, void *, int, unsigned, unsigned, unsigned,
                               const void *, const void *, int);
    typedef void (*StreamWrite)(void *, const void *, unsigned);
    typedef int (*StreamSend)(void *, int);
    typedef void (*StreamDtor)(void *);

    static void *run(void *unused);
    bool load(const char *library);
    bool resolveSymbols();

    core::Mutex sendLock_;
    ConnectionCtor connectionCtor_;
    ConnectionAdd connectionAdd_;
    ConnectionStart connectionStart_;
    ClientCtor clientCtor_;
    ClientNotify notifyFn_;
    StreamCtor streamCtor_;
    StreamWrite streamWrite_;
    StreamSend streamSend_;
    StreamDtor streamDtor_;
    void *vtable_;
    std::unique_ptr<unsigned char[]> connection_;
    std::unique_ptr<unsigned char[]> client_;
    void *slots_[platform::dsi_abi::kVtableSlots];
    nbt_dsi_handlers handlers_;
    void *library_;
    bool started_;
    bool attached_;
};

} // namespace most
} // namespace nbt

#endif
