#pragma once
#include "engine/hooks/hook_set.h"

namespace sentinel::save {
// deferred save work keeps this entry, so it acquires a fresh trampoline lease on each call.
template <hooks::HookSet& Group, size_t Site, class F> struct HookOriginal;
template <hooks::HookSet& Group, size_t Site, class R, class... Args> struct HookOriginal<Group, Site, R (*)(Args...)> {
    static R call(Args... args) {
        const auto lease = Group.enter(Site);
        __try {
            return reinterpret_cast<R (*)(Args...)>(lease.original)(args...);
        } __finally {
            Group.leave(lease);
        }
    }
};
} // namespace sentinel::save
