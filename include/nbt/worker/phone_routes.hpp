#ifndef NBT_WORKER_PHONE_ROUTES_HPP
#define NBT_WORKER_PHONE_ROUTES_HPP
#include "nbt/core/span.hpp"
namespace nbt {
namespace worker {
class Worker;
int phoneApplications(Worker &worker, core::Span<char> output);
int phoneSettings(Worker &worker, const char *body, bool setting, core::Span<char> output);
} // namespace worker
} // namespace nbt
#endif
