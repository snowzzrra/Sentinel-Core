#pragma once
#include "save_installation.h"
#include "save_b_trace.h"
#include <MinHook.h>
#include <vector>

namespace sentinel::hooks {
enum class State { empty, preparing, installed, stopping, cleanup_failed };
enum class Operation { none, create, enable, disable, remove, cancel };
struct HookSpec {
    uintptr_t target;
    void* detour;
    void** original;
    uint32_t id = SC_INSTALL_UNKNOWN;
    uint32_t rva = 0;
    bool selected = true; // optional sites keep their slot for delayed entries.
};
template<class F> HookSpec bind(uintptr_t target, F detour, F& original) {
    return {target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original)};
}
struct Error { Operation operation; size_t site; MH_STATUS status; };
struct SiteSnapshot { uintptr_t target; bool created, enabled; size_t entries; };
struct Snapshot {
    const char* group;
    State state;
    Error first_error;
    std::vector<Error> cleanup_errors;
    std::vector<SiteSnapshot> sites;
    sc_save_installation_snapshot installation;
};
struct Lease { size_t site; void* original; bool committed, counted; };
struct Api {
    decltype(&MH_CreateHook) create = MH_CreateHook;
    decltype(&MH_EnableHook) enable = MH_EnableHook;
    decltype(&MH_DisableHook) disable = MH_DisableHook;
    decltype(&MH_RemoveHook) remove = MH_RemoveHook;
};
class HookSet;
class Transaction {
public:
    Transaction();
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();
    bool rollback();
private:
    friend class HookSet;
    void track(HookSet* group);
    Transaction* previous_;
    std::vector<HookSet*> groups_;
    bool finished_ = false, clean_ = true;
};
class HookSet {
public:
    explicit HookSet(const char* group, Api api = {}, save::Installation* record = nullptr, uint32_t diagnostic_group = 0)
        : group_(group), api_(api), diagnostic_group_(diagnostic_group), record_(record ? record : &local_record_) {}
    HookSet(const HookSet&) = delete;
    HookSet& operator=(const HookSet&) = delete;
    bool install(const HookSpec* specs, size_t count, HANDLE stop = nullptr);
    template<size_t N> bool install(const HookSpec (&specs)[N], HANDLE stop = nullptr) {
        return install(specs, N, stop);
    }
    bool uninstall();
    Lease enter(size_t site);
    void leave(Lease lease);
    Snapshot inspect() const;
    save::BSnapshot diagnostics() const { return trace_.snapshot(); }
private:
    struct Site {
        HookSpec spec;
        void* trampoline = nullptr;
        bool created = false, enabled = false;
        size_t entries = 0;
    };
    bool prepare(const HookSpec*, size_t count, bool& installed);
    bool validate(const HookSpec*, size_t count);
    bool cancelled(HANDLE stop);
    bool create_sites(HANDLE stop);
    bool enable_sites(HANDLE stop);
    void disable_sites();
    bool cleanup();
    void error(Operation operation, size_t site, MH_STATUS status, bool cleanup);
    const char* group_;
    Api api_;
    uint32_t diagnostic_group_;
    SRWLOCK lifecycle_ = SRWLOCK_INIT;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    CONDITION_VARIABLE changed_ = CONDITION_VARIABLE_INIT;
    State state_ = State::empty;
    Error first_error_{Operation::none, 0, MH_OK};
    std::vector<Error> cleanup_errors_;
    std::vector<Site> sites_;
    save::Installation local_record_;
    save::Installation* record_;
    save::BTrace trace_;
};
}
