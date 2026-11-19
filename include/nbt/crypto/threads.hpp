// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CRYPTO_THREADS_HPP
#define NBT_CRYPTO_THREADS_HPP

namespace nbt {
namespace crypto {

/* OpenSSL 0.9.8 (the QNX 6.5 SDK's 0.9.8m) is safe in several threads only
 * once a locking and a thread id callback are set (threads(3)). The worker
 * calls this before it starts any thread; later calls do nothing. OpenSSL 1.1
 * and later lock by themselves (the host build): nothing to set. Returns
 * false when the locks cannot be allocated. */
bool initializeThreads();

} // namespace crypto
} // namespace nbt

#endif
