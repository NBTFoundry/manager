// SPDX-License-Identifier: MPL-2.0
#include "nbt/most/input.hpp"

#include "nbt/core/system.hpp"
#include "nbt/dsi.h"
#include "nbt/log.h"
#include "nbt/most/codec.hpp"
#include "nbt/most/dsi_client.hpp"
#include "registry_log.hpp"

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

namespace {

using nbt::most::MostInput;

const size_t kMaxPayload = 4096;
const char kClientName[] = "NBTMGR_INPUT";

std::aligned_storage<sizeof(MostInput), alignof(MostInput)>::type inputStorage;
MostInput *inputInstance = NULL;

void attached(void *) {
    MostInput *input = MostInput::shared();
    if (input) {
        input->onAttached();
    }
}

void detached(void *) {
    MostInput *input = MostInput::shared();
    if (input) {
        input->onDetached();
    }
}

void data(uint32_t id, const unsigned char *payload, uint32_t length, void *) {
    MostInput *input = MostInput::shared();
    if (input) {
        input->onData(id, payload, length);
    }
}

bool openPipe(nbt::core::UniqueFd &reader, nbt::core::UniqueFd &writer) {
    int fds[2] = {-1, -1};
    if (pipe(fds)) {
        return false;
    }
    reader.reset(fds[0]);
    writer.reset(fds[1]);
    return !fcntl(reader.get(), F_SETFL, O_NONBLOCK) && !fcntl(writer.get(), F_SETFL, O_NONBLOCK) &&
           !fcntl(reader.get(), F_SETFD, FD_CLOEXEC) && !fcntl(writer.get(), F_SETFD, FD_CLOEXEC);
}

} // namespace

namespace nbt {
namespace most {

MostInput::MostInput()
    : ccs_(), volume_(), writer_(), volumeWriter_(), volumeReader_(), releaseLock_(),
      registered_(false), started_(false), releaseWanted_(false), releaseDone_(false),
      backHeld_(false), backKeyHeld_(false), volumeSeen_(false), volumePct_(0), volumeSeq_(0),
      registeredClient_(0), registryHash_(0), registryLogged_(false) {
}

MostInput *MostInput::shared() {
    return inputInstance;
}
MostInput *MostInput::sharedOrCreate() {
    if (!inputInstance) {
        inputInstance = new (&inputStorage) nbt::most::MostInput();
    }
    return inputInstance;
}

void MostInput::pushInputs(const nbt_input *inputs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const unsigned char byte = (unsigned char)inputs[i];
        ssize_t n;
        do {
            n = write(writer_.get(), &byte, 1);
        } while (n < 0 && errno == EINTR);
        if (n != 1) {
            return;
        }
    }
}

size_t MostInput::controlKeys(uint16_t *keys) const {
    for (size_t i = 0; i < ccs_.config().instance_count; ++i) {
        keys[i] = (uint16_t)(0xe500u | ccs_.config().instances[i]);
    }
    return ccs_.config().instance_count;
}

bool MostInput::releasing() {
    bool wanted = false;
    bool done = false;
    {
        core::LockGuard lock(releaseLock_);
        wanted = releaseWanted_;
        done = releaseDone_;
    }
    if (!wanted) {
        return false;
    }
    if (!done) {
        if (registered_) {
            unsigned char payload[96];
            uint16_t keys[NBT_CCS_INSTANCES_MAX];
            const long n = Codec::unregisterControls(registeredClient_, keys, controlKeys(keys),
                                                     payload, sizeof(payload));
            if (n > 0) {
                (void)nbt_dsi_request(NBT_MOST_REQ_UNREGISTER_CONTROLS, payload, (size_t)n);
                nbt_logf("MOST controls released");
            } else {
                nbt_logf("MOST release request could not be built");
            }
        } else {
            nbt_logf("MOST release: nothing registered");
        }
        registered_ = false;
        core::LockGuard lock(releaseLock_);
        releaseDone_ = true;
    }
    return true;
}

void MostInput::logRegistry(const unsigned char *bytes, uint32_t length) {
    logMostRegistry(bytes, length, registryLogged_, registryHash_);
}

void MostInput::onAttached() {
    unsigned char payload[96];
    registered_ = false;
    if (releasing()) {
        return;
    }
    nbt_logf("MOST attached; registering client");
    const long length = Codec::registerClient(kClientName, payload, sizeof(payload));
    if (length > 0) {
        (void)nbt_dsi_request(NBT_MOST_REQ_REGISTER_CLIENT, payload, (size_t)length);
    }
    for (size_t i = 0; i < nbt_most_subscription_count; ++i) {
        (void)nbt_dsi_notify(nbt_most_subscriptions[i]);
    }
}
void MostInput::onDetached() {
    registered_ = registryLogged_ = false;
    ccs_.reset();
    volume_.reset();
    {
        core::LockGuard lock(releaseLock_);
        backHeld_ = false;
        backKeyHeld_ = false;
    }
    nbt_logf("MOST detached");
    (void)releasing();
}
void MostInput::onData(uint32_t id, const unsigned char *bytes, uint32_t length) {
    nbt_most_message message;
    nbt_input inputs[NBT_CCS_MAX_STEP + 4];
    if (releasing() || length > kMaxPayload) {
        return;
    }
    if (id == NBT_MOST_ATTR_REGISTRY) {
        logRegistry(bytes, length);
    } else if (id == NBT_MOST_INFO_CLIENT && !registered_) {
        unsigned char payload[96];
        uint16_t keys[NBT_CCS_INSTANCES_MAX];
        uint32_t client = 0;
        if (Codec::clientId(bytes, length, &client)) {
            nbt_logf("MOST client registration refused");
            return;
        }
        (void)nbt_dsi_notify(NBT_MOST_ATTR_COMM_STATE);
        const long n =
            Codec::registerControls(client, keys, controlKeys(keys), payload, sizeof(payload));
        if (n > 0) {
            (void)nbt_dsi_request(NBT_MOST_REQ_REGISTER_CONTROLS, payload, (size_t)n);
            registered_ = true;
            registeredClient_ = client;
            nbt_logf("MOST client %lu; controls registered for %u instances", (unsigned long)client,
                     (unsigned)ccs_.config().instance_count);
        } else {
            nbt_logf("MOST controls request could not be built");
        }
    } else if (id == NBT_MOST_INFO_MESSAGE && !Codec::parse(bytes, length, &message)) {
        const size_t n =
            ccs_.feed(message.fblock, message.inst, message.fkt, message.op, message.data,
                      message.length, inputs, sizeof(inputs) / sizeof(inputs[0]));
        {
            core::LockGuard lock(releaseLock_);
            backHeld_ = ccs_.backHeld();
            backKeyHeld_ = ccs_.backKeyHeld();
        }
        pushInputs(inputs, n);
        const core::Result<uint64_t> now = core::monotonicMs();
        trace_.record(now.get() ? now.value() : 0, message.fblock, message.inst, message.fkt,
                      message.op, message.data, message.length, n);
        int pct = 0;
        if (now.get() && volume_.feed(message.fblock, message.fkt, message.op, message.data,
                                      message.length, now.value(), pct)) {
            {
                core::LockGuard lock(releaseLock_);
                volumeSeen_ = true;
                volumePct_ = pct;
                ++volumeSeq_;
            }
            const unsigned char wake = 'v';
            if (volumeWriter_.valid()) {
                const ssize_t sent = write(volumeWriter_.get(), &wake, 1);
                (void)sent;
            }
        }
    }
}

int MostInput::start(const char *path, const nbt_ccs_config *config) {
    if (started_ || (config && !Ccs::valid(config))) {
        return -1;
    }
    core::UniqueFd reader, writer, volReader, volWriter;
    if (!openPipe(reader, writer) || !openPipe(volReader, volWriter)) {
        return -1;
    }
    ccs_ = Ccs(config);
    writer_ = std::move(writer);
    volumeReader_ = std::move(volReader);
    volumeWriter_ = std::move(volWriter);
    nbt_dsi_handlers handlers;
    memset(&handlers, 0, sizeof(handlers));
    handlers.attached = attached;
    handlers.detached = detached;
    handlers.data = data;
    if (nbt_dsi_start(path, &handlers)) {
        writer_.reset();
        volumeWriter_.reset();
        volumeReader_.reset();
        return -1;
    }
    started_ = true;
    return reader.release();
}
StopStatus MostInput::stop(unsigned timeoutMs) {
    if (!started_ || !ccs_.config().release) {
        return StopIdle;
    }
    {
        core::LockGuard lock(releaseLock_);
        releaseWanted_ = true;
    }
    for (unsigned waited = 0;; waited += 10) {
        const struct timespec pause = {0, 10 * 1000000L};
        {
            core::LockGuard lock(releaseLock_);
            if (releaseDone_) {
                return StopReleased;
            }
        }
        if (waited >= timeoutMs) {
            return StopTimedOut;
        }
        nanosleep(&pause, NULL);
    }
}
bool MostInput::backHeld() {
    core::LockGuard lock(releaseLock_);
    return backHeld_;
}
bool MostInput::backKeyHeld() {
    core::LockGuard lock(releaseLock_);
    return backKeyHeld_;
}
int MostInput::volumeFd() const {
    return volumeReader_.get();
}
bool MostInput::latestVolume(int &percent, uint32_t &sequence) {
    core::LockGuard lock(releaseLock_);
    if (!volumeSeen_) {
        return false;
    }
    percent = volumePct_;
    sequence = volumeSeq_;
    return true;
}

void MostInput::logTrace(const char *why) {
    const core::Result<uint64_t> now = core::monotonicMs();
    (void)trace_.dump(why, now.get() ? now.value() : 0);
}

} // namespace most
} // namespace nbt
