#ifndef NBT_PROCESS_SPAWN_HPP
#define NBT_PROCESS_SPAWN_HPP

#include "nbt/core/status.hpp"

#include <sys/types.h>

namespace nbt {
namespace process {
namespace spawn {

struct ChildPlan {
    const char *executable;
    char *arguments[6];
};

core::Status prepareHost(ChildPlan *plan, char *executable, const char *app, bool automatic,
                         char *testRoot);
core::Status detach(const ChildPlan &plan, pid_t *intermediate);
void execChild(const ChildPlan &plan) __attribute__((noreturn));

} // namespace spawn
} // namespace process
} // namespace nbt

#endif
