#ifndef NBT_WORKER_PHONE_ACTIONS_HPP
#define NBT_WORKER_PHONE_ACTIONS_HPP
#include "nbt/storage/phone_store.hpp"
namespace nbt {
namespace worker {
class Worker;
class PhoneActions {
public:
    PhoneActions(Worker &worker, storage::PhoneStore &phones);
    void expire(uint64_t now);
    bool canPair(uint64_t now) const {
        return pairDeadline_ > now && !pendingDeadline_;
    }
    bool pending() const {
        return pendingDeadline_ != 0;
    }
    bool candidate(const char *id, storage::Phone &phone) const;
    static bool parsePair(const char *body, storage::Phone &phone);
    int pair(const char *key, const char *nonce, const char *body, uint64_t now,
             core::Span<char> out);
    int remove(const char *key, const char *body, uint64_t now, core::Span<char> out);
    void uploaded(const char *key, uint64_t now);
    int status(const char *key, uint64_t now, const char *bssid, const char *host,
               core::Span<char> out);
    bool local(const char *command, uint64_t now, core::Span<char> out);

private:
    bool confirm(const char *id, bool yes, uint64_t now);
    Worker &worker_;
    storage::PhoneStore &phones_;
    uint64_t pairDeadline_, pendingDeadline_, resultDeadline_;
    storage::Phone candidate_;
    char resultMessage_[128];
    char owner_[65], pendingApp_[49], result_[32], code_[7], actionId_[33];
};
} // namespace worker
} // namespace nbt
#endif
