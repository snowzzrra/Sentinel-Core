#include "hook_set.h"
#include <algorithm>

namespace sentinel::hooks {
namespace { thread_local Transaction* transaction = nullptr; }
Transaction::Transaction() : previous_(transaction) { transaction = this; }
Transaction::~Transaction() {
    if (!finished_) rollback();
    transaction = previous_;
}
void Transaction::commit() {
    if (finished_) return;
    if (previous_)
        for (auto* group : groups_) previous_->track(group);
    finished_ = true;
}
void Transaction::track(HookSet* group) {
    if (!finished_ && std::find(groups_.begin(), groups_.end(), group) == groups_.end()) groups_.push_back(group);
}
bool Transaction::rollback() {
    if (finished_) return clean_;
    finished_ = true;
    for (auto group = groups_.rbegin(); group != groups_.rend(); ++group) clean_ = (*group)->uninstall() && clean_;
    return clean_;
}
void HookSet::error(Operation operation, size_t site, MH_STATUS status, bool cleanup_error) {
    AcquireSRWLockExclusive(&lock_);
    const Error failure{operation, site, status};
    if (cleanup_error) cleanup_errors_.push_back(failure);
    else if (first_error_.operation == Operation::none) first_error_ = failure;
    ReleaseSRWLockExclusive(&lock_);
    trace_.record(save::BStage::native_start, save::BStatus::refused, group_, 0,
        {{"operation", static_cast<unsigned>(operation)}, {"site", site},
         {"minhook", static_cast<unsigned>(status)}, {"cleanup", cleanup_error}});
}
bool HookSet::prepare(const HookSpec* specs, size_t count, bool& installed) {
    AcquireSRWLockExclusive(&lock_);
    const bool same_sites = count == sites_.size() &&
        std::equal(sites_.begin(), sites_.end(), specs, [](const Site& a, const HookSpec& b) {
            return a.spec.target == b.target && a.spec.detour == b.detour && a.spec.original == b.original &&
                a.spec.id == b.id && a.spec.rva == b.rva;
        });
    // delayed entries need the same restored targets across retries.
    if (state_ != State::empty || (!sites_.empty() && !same_sites)) {
        installed = state_ == State::installed && same_sites &&
                    std::equal(sites_.begin(), sites_.end(), specs,
                               [](const Site& a, const HookSpec& b) { return a.spec.selected == b.selected; });
        ReleaseSRWLockExclusive(&lock_);
        return false;
    }
    sites_.clear();
    if (transaction) transaction->track(this);
    for (size_t i = 0; i < count; ++i) sites_.push_back({specs[i]});
    first_error_ = {Operation::none, 0, MH_OK};
    cleanup_errors_.clear();
    state_ = State::preparing;
    ReleaseSRWLockExclusive(&lock_);
    return true;
}
bool HookSet::validate(const HookSpec* specs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const auto& spec = sites_[i].spec;
        if (!spec.target || !spec.detour || !spec.original ||
            std::any_of(specs, specs + i, [&](const HookSpec& prior) { return prior.target == spec.target; })) {
            error(Operation::create, i, MH_ERROR_UNSUPPORTED_FUNCTION, false);
            return false;
        }
    }
    return true;
}
bool HookSet::cancelled(HANDLE stop) {
    if (!stop || WaitForSingleObject(stop, 0) == WAIT_TIMEOUT) return false;
    error(Operation::cancel, 0, MH_OK, false);
    return true;
}
bool HookSet::create_sites(HANDLE stop) {
    for (size_t i = 0; i < sites_.size(); ++i) {
        if (cancelled(stop)) return false;
        auto& site = sites_[i];
        if (!site.spec.selected) continue;
        void* trampoline = nullptr;
        const auto status = record_->hook(SC_INSTALL_NATIVE_CREATE, diagnostic_group_,
            site.spec.id == SC_INSTALL_UNKNOWN ? static_cast<uint32_t>(i) : site.spec.id, site.spec.rva,
            [&] { return api_.create(reinterpret_cast<void*>(site.spec.target), site.spec.detour, &trampoline); });
        if (status != MH_OK) {
            error(Operation::create, i, static_cast<MH_STATUS>(status), false);
            return false;
        }
        AcquireSRWLockExclusive(&lock_);
        site.created = true; site.trampoline = trampoline; *site.spec.original = trampoline;
        ReleaseSRWLockExclusive(&lock_);
    }
    return true;
}
bool HookSet::enable_sites(HANDLE stop) {
    for (size_t i = 0; i < sites_.size(); ++i) {
        if (cancelled(stop)) return false;
        auto& site = sites_[i];
        if (!site.created) continue;
        const auto status = record_->hook(SC_INSTALL_NATIVE_ENABLE, diagnostic_group_,
            site.spec.id == SC_INSTALL_UNKNOWN ? static_cast<uint32_t>(i) : site.spec.id, site.spec.rva,
            [&] { return api_.enable(reinterpret_cast<void*>(site.spec.target)); });
        AcquireSRWLockExclusive(&lock_);
        site.enabled = status == MH_OK || status == MH_ERROR_ENABLED;
        ReleaseSRWLockExclusive(&lock_);
        if (status != MH_OK) {
            error(Operation::enable, i, static_cast<MH_STATUS>(status), false);
            return false;
        }
    }
    return true;
}
bool HookSet::install(const HookSpec* specs, size_t count, HANDLE stop) {
    AcquireSRWLockExclusive(&lifecycle_);
    bool installed = false;
    if (!prepare(specs, count, installed)) {
        ReleaseSRWLockExclusive(&lifecycle_);
        return installed;
    }
    const bool ok = validate(specs, count) && create_sites(stop) && enable_sites(stop) && !cancelled(stop);
    if (ok) {
        AcquireSRWLockExclusive(&lock_);
        state_ = State::installed;
        WakeAllConditionVariable(&changed_); ReleaseSRWLockExclusive(&lock_);
    } else cleanup();
    ReleaseSRWLockExclusive(&lifecycle_);
    return ok;
}
void HookSet::disable_sites() {
    for (size_t i = sites_.size(); i-- > 0;) {
        auto& site = sites_[i];
        if (!site.created || !site.enabled) continue;
        const auto event = record_->begin(SC_INSTALL_REMOVE, diagnostic_group_,
            site.spec.id == SC_INSTALL_UNKNOWN ? static_cast<uint32_t>(i) : site.spec.id, site.spec.rva);
        const auto status = api_.disable(reinterpret_cast<void*>(site.spec.target));
        record_->finish(event, status == MH_OK || status == MH_ERROR_DISABLED ? SC_NATIVE_NONE : SC_NATIVE_HOOK_FAILED,
            static_cast<uint32_t>(status));
        if (status != MH_OK && status != MH_ERROR_DISABLED) {
            error(Operation::disable, i, static_cast<MH_STATUS>(status), true); continue;
        }
        AcquireSRWLockExclusive(&lock_); site.enabled = false; ReleaseSRWLockExclusive(&lock_);
    }
}
bool HookSet::cleanup() {
    AcquireSRWLockExclusive(&lock_);
    state_ = State::stopping;
    WakeAllConditionVariable(&changed_);
    ReleaseSRWLockExclusive(&lock_);
    disable_sites();
    AcquireSRWLockExclusive(&lock_);
    // a detour can call another site's original within the same group.
    while (std::any_of(sites_.begin(), sites_.end(), [](const Site& site) { return site.entries != 0; }))
        SleepConditionVariableSRW(&changed_, &lock_, INFINITE, 0);
    ReleaseSRWLockExclusive(&lock_);
    for (size_t i = sites_.size(); i-- > 0;) {
        auto& site = sites_[i];
        if (!site.created || site.enabled) continue;
        const auto status = record_->hook(SC_INSTALL_REMOVE, diagnostic_group_,
            site.spec.id == SC_INSTALL_UNKNOWN ? static_cast<uint32_t>(i) : site.spec.id, site.spec.rva,
            [&] { return api_.remove(reinterpret_cast<void*>(site.spec.target)); });
        if (status != MH_OK) { error(Operation::remove, i, static_cast<MH_STATUS>(status), true); continue; }
        AcquireSRWLockExclusive(&lock_);
        site.created = false; site.trampoline = nullptr; *site.spec.original = nullptr;
        ReleaseSRWLockExclusive(&lock_);
    }
    AcquireSRWLockExclusive(&lock_);
    const bool clean = std::none_of(sites_.begin(), sites_.end(), [](const Site& site) { return site.created; });
    state_ = clean ? State::empty : State::cleanup_failed;
    WakeAllConditionVariable(&changed_); ReleaseSRWLockExclusive(&lock_);
    return clean;
}
bool HookSet::uninstall() {
    AcquireSRWLockExclusive(&lifecycle_);
    const auto clean = cleanup();
    ReleaseSRWLockExclusive(&lifecycle_);
    return clean;
}
Lease HookSet::enter(size_t index) {
    AcquireSRWLockExclusive(&lock_);
    while (state_ == State::preparing) SleepConditionVariableSRW(&changed_, &lock_, INFINITE, 0);
    auto& site = sites_.at(index);
    const bool counted = site.enabled;
    if (counted) ++site.entries;
    const Lease lease{index, counted ? site.trampoline : reinterpret_cast<void*>(site.spec.target),
                      state_ == State::installed && site.enabled, counted};
    ReleaseSRWLockExclusive(&lock_);
    return lease;
}
void HookSet::leave(Lease lease) {
    if (!lease.counted) return;
    AcquireSRWLockExclusive(&lock_);
    --sites_[lease.site].entries;
    WakeAllConditionVariable(&changed_); ReleaseSRWLockExclusive(&lock_);
}
Snapshot HookSet::inspect() const {
    AcquireSRWLockShared(&lock_);
    Snapshot out{group_, state_, first_error_, cleanup_errors_, {}, record_->inspect()};
    for (const auto& site : sites_) out.sites.push_back({site.spec.target, site.created, site.enabled, site.entries});
    ReleaseSRWLockShared(&lock_);
    return out;
}
}
