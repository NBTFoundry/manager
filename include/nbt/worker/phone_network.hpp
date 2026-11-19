#ifndef NBT_WORKER_PHONE_NETWORK_HPP
#define NBT_WORKER_PHONE_NETWORK_HPP
#include "nbt/storage/phone_store.hpp"
namespace nbt {
namespace worker {
void phoneBssid(const char *interfaceName, char output[18]);
bool interfaceReady(const char *interfaceName, char ipOut[40]);
int phoneWake(storage::PhoneStore &phones, const char *path, uint64_t &lastWake, uint64_t now,
              core::Span<char> out);
} // namespace worker
} // namespace nbt
#endif
