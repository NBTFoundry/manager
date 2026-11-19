#include "nbt/most/input.hpp"
#include "nbt/most_input.h"

namespace {
nbt::core::StaticMutex apiLock = NBT_STATIC_MUTEX_INITIALIZER;
}

extern "C" {

int nbt_most_input_start(const char *path, const nbt_ccs_config *config) {
    nbt::core::LockGuard lock(apiLock);
    return nbt::most::MostInput::sharedOrCreate()->start(path, config);
}
int nbt_most_input_stop(unsigned timeoutMs) {
    nbt::core::LockGuard lock(apiLock);
    nbt::most::MostInput *input = nbt::most::MostInput::shared();
    return input ? input->stop(timeoutMs) : 0;
}
int nbt_most_input_back_held(void) {
    nbt::core::LockGuard lock(apiLock);
    nbt::most::MostInput *input = nbt::most::MostInput::shared();
    return input && input->backHeld() ? 1 : 0;
}
int nbt_most_input_back_key_held(void) {
    nbt::core::LockGuard lock(apiLock);
    nbt::most::MostInput *input = nbt::most::MostInput::shared();
    return input && input->backKeyHeld() ? 1 : 0;
}
void nbt_most_input_log_recent(const char *why) {
    nbt::core::LockGuard lock(apiLock);
    nbt::most::MostInput *input = nbt::most::MostInput::shared();
    if (input) {
        input->logTrace(why);
    }
}
int nbt_most_input_volume_fd(void) {
    nbt::core::LockGuard lock(apiLock);
    nbt::most::MostInput *input = nbt::most::MostInput::shared();
    return input ? input->volumeFd() : -1;
}
int nbt_most_input_volume(int *percent, uint32_t *sequence) {
    nbt::core::LockGuard lock(apiLock);
    nbt::most::MostInput *input = nbt::most::MostInput::shared();
    int pct = 0;
    uint32_t seq = 0;
    if (!input || !input->latestVolume(pct, seq)) {
        return 0;
    }
    if (percent) {
        *percent = pct;
    }
    if (sequence) {
        *sequence = seq;
    }
    return 1;
}

} // extern "C"
