#ifndef NBT_CORE_SCOPE_EXIT_HPP
#define NBT_CORE_SCOPE_EXIT_HPP

#include <utility>

namespace nbt {
namespace core {

template <typename F> class ScopeExit {
public:
    explicit ScopeExit(F function) : function_(std::move(function)), active_(true) {
    }
    ScopeExit(ScopeExit &&other) : function_(std::move(other.function_)), active_(other.active_) {
        other.dismiss();
    }
    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
    ~ScopeExit() {
        if (active_) {
            function_();
        }
    }
    void dismiss() {
        active_ = false;
    }

private:
    F function_;
    bool active_;
};

template <typename F> ScopeExit<F> onScopeExit(F function) {
    return ScopeExit<F>(std::move(function));
}

} // namespace core
} // namespace nbt

#endif
