// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_PHONE_API_HPP
#define NBT_WORKER_PHONE_API_HPP
#include "nbt/storage/phone_store.hpp"
#include "nbt/worker/http.hpp"
#include "nbt/worker/phone_actions.hpp"
#include "nbt/worker/phone_auth.hpp"
#include "nbt/worker/phone_logs.hpp"
namespace nbt {
namespace worker {
class Worker;
class PhoneApi {
public:
    explicit PhoneApi(Worker &worker);
    bool door() const {
        return door_;
    }
    bool initialize(bool door = false);
    void address(const char *host, const char *interfaceName);
    bool begin(const http::Request &request, uint64_t now);
    bool authorized(const http::Request &request, const unsigned char hash[32], const char *body);
    int execute(const http::Request &request, const char *body, uint64_t now,
                core::Span<char> output);
    int finishUpload(const http::Request &request, uint64_t now, core::Span<char> output);
    /* The log routes, replied from files (phone_logs.hpp). The system log is
     * one at a time: systemLogDone ends the one started. */
    int logs(const http::Request &request, const char *body, uint64_t now, LogReply &reply);
    void systemLogDone() {
        systemLog_ = false;
    }
    core::Result<core::UniqueFd> uploadStart();
    void uploadAbort();
    bool nextNonce(uint64_t now, char output[65]) {
        return nonces_.issue(now, output);
    }
    bool local(const char *command, uint64_t now, core::Span<char> output);

private:
    Worker &worker_;
    storage::ManagedRoot &root_;
    storage::PhoneStore phones_;
    PhoneNonces nonces_;
    PhoneActions actions_;
    bool door_;
    bool receiving_;
    bool systemLog_;
    uint64_t wakeMs_;
    char bssid_[18];
    char host_[128];
    char wakePath_[1024];
};
} // namespace worker
} // namespace nbt
#endif
