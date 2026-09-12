// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_session.h"
#include <windows.h>
#include <cstring>

namespace sentinel::save {
namespace { thread_local uintptr_t native_route_caller = 0, native_route_rva = 0; }
NativeRouteScope::NativeRouteScope(uintptr_t caller, uintptr_t image) : previous(native_route_caller), previous_rva(native_route_rva) {
    native_route_caller = caller; native_route_rva = image && caller >= image ? caller-image : 0;
}
NativeRouteScope::~NativeRouteScope() { native_route_caller = previous; native_route_rva = previous_rva; }
void Session::begin_profile(uintptr_t data) {
    std::lock_guard<std::mutex> guard(profile_trace_mutex_);
    if (profile_trace_.request) return; // Retain the first startup request through later refusals.
    profile_trace_.request = 1; profile_data_ = data;
    profile_trace_.identity_matched = native_io(); // Native hook's provider_operation failure closes native_io.
}
bool Session::is_profile_request(uintptr_t data) const {
    std::lock_guard<std::mutex> guard(profile_trace_mutex_);
    return data && data == profile_data_;
}
void Session::profile_step(ProfileStage stage, ProfileStatus status, const char* predicate,
        bool attempted, int64_t state, int64_t outcome, uint32_t value, ProfileRead read) {
    std::lock_guard<std::mutex> guard(profile_trace_mutex_);
    auto& step = profile_trace_.steps[static_cast<size_t>(stage)];
    if (step.status == status && step.predicate == predicate && step.native_attempted == attempted &&
        step.native_state == state && step.native_outcome == outcome && step.native_value == value &&
        step.read.reason == read.reason && step.read.error == read.error && step.read.requested == read.requested &&
        step.read.offset == read.offset && step.read.size == read.size) return;
    const auto now = GetTickCount64();
    if (!step.first_ms) step.first_ms = now;
    step.changed_ms = now; step.status = status; step.predicate = predicate;
    step.native_attempted = attempted; step.native_state = state; step.native_outcome = outcome; step.native_value = value;
    step.read = read;
    if (status == ProfileStatus::refused) {
        if (profile_trace_.failed_stage == ProfileStage::count) {
            profile_trace_.failed_stage = stage; profile_trace_.failure = step;
        } else if (profile_trace_.downstream_refusals != UINT32_MAX) ++profile_trace_.downstream_refusals;
    }
}
ProfileTrace Session::profile_trace() const {
    std::lock_guard<std::mutex> guard(profile_trace_mutex_); return profile_trace_;
}
Session::~Session() {
    // Production's pinned process owner is never destructed. Controlled owners
    // leave scope on their startup thread; inspection shutdown releases neither lease.
    if (native_root_lock_) { ReleaseMutex(native_root_lock_); CloseHandle(native_root_lock_); }
}
bool steam_name_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    const auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; };
    for (size_t i = 0; i < a.size(); ++i) if (fold(a[i]) != fold(b[i])) return false;
    return true;
}
storage::Result Session::configure(const storage::Descriptor& descriptor,
                                  std::unique_ptr<storage::Namespace> lease) {
    std::unique_lock<std::mutex> guard(mutex_);
    if (attempted_) return {storage::Outcome::ownership_conflict, 0};
    attempted_ = true;
    std::string hash;
    auto result = storage::namespace_id(descriptor.identity, hash);
    if (!result.ok() || !lease || lease->metadata().namespace_id != hash) {
        fault_ = SessionFault::descriptor; state_ = SessionState::rejected;
        return result.ok() ? storage::Result{storage::Outcome::identity_mismatch, 0} : result;
    }
    namespace_id_ = hash;
    native_root_ = "ap-" + hash.substr(0, 40);
    constexpr char hex[] = "0123456789abcdef";
    std::string seed;
    for (unsigned char byte : descriptor.identity.seed) {
        seed += hex[byte >> 4]; seed += hex[byte & 15];
    }
    ownership_ = "sentinel-native-session-v1\nnamespace_id=" + hash + "\nseed_hex=" + seed +
        "\nteam=" + std::to_string(*descriptor.identity.team) +
        "\nslot=" + std::to_string(*descriptor.identity.slot) +
        "\ngeneration_fingerprint=" + descriptor.identity.generation_fingerprint +
        "\nprovenance=synthetic-fixture\n";
    lease_ = std::move(lease);
    guard.unlock(); // Campaign refusal records a Session fault through its own lock.
    if (!campaign_run.configure(*this, descriptor, *lease_)) return {storage::Outcome::invalid_descriptor, 0};
    state_.store(SessionState::prepared, std::memory_order_release);
    return {storage::Outcome::ok, 0};
}
void Session::reject(SessionFault why) {
    std::lock_guard<std::mutex> guard(mutex_);
    attempted_ = true;
    if (fault_ == SessionFault::none) fault_ = why;
    requests_ = false;
    state_ = routed() ? SessionState::faulted : SessionState::rejected;
}
void Session::install(uintptr_t root, uintptr_t startup_return, uint32_t routes) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ != SessionState::prepared) return;
    root_ = root; caller_ = startup_return; routes_ = routes;
}
bool Session::startup_enter(uintptr_t root, uintptr_t caller, uint32_t thread) {
    installation.startup(1);
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ == SessionState::disabled) return false;
    SessionFault why = SessionFault::none;
    if (!entered_ && state_ == SessionState::prepared && root_ && caller_ &&
        root == root_ && caller == caller_ && thread) qualified_ = true;
    if (entered_ || state_ != SessionState::prepared) why = SessionFault::repeated_startup;
    else if (!root_ || !caller_ || root != root_ || caller != caller_ || !thread)
        why = SessionFault::startup_context;
    else if (routes_ != required_routes) why = SessionFault::incomplete_routes;
    entered_ = true;
    if (why != SessionFault::none) {
        if (fault_ == SessionFault::none) fault_ = why;
        state_ = routed() ? SessionState::faulted : SessionState::rejected;
        requests_ = false; return false;
    }
    startup_thread_ = thread;
    state_.store(SessionState::starting, std::memory_order_release);
    return true;
}
void Session::startup_leave(bool abnormal) {
    profile_step(ProfileStage::root, abnormal ? ProfileStatus::refused : ProfileStatus::succeeded,
        abnormal ? "abnormal_root_return" : "qualified_root_return");
    std::lock_guard<std::mutex> guard(mutex_);
    if (abnormal) {
        if (fault_ == SessionFault::none) fault_ = SessionFault::provider_identity;
        state_ = routed() ? SessionState::faulted : SessionState::rejected;
        requests_ = false;
    } else {
        // The manager constructor initializes its provider before RootInit
        // publishes root+9b38. Qualification survives normal return until the
        // first import boundary; observing an unrouted import closes it forever.
        root_finished_ = qualified_;
        if (state_ == SessionState::binding && profile_finished_) {
            state_ = SessionState::admitted; requests_ = !requests_stopped_;
            profile_step(ProfileStage::admission, ProfileStatus::succeeded, "profile_and_root_completed");
        }
    }
}
UnroutedTrace Session::unrouted_trace() const {
    std::lock_guard<std::mutex> guard(mutex_); return unrouted_;
}
bool Session::pre_root_profile_query(uintptr_t expected_caller, uintptr_t identity) {
    std::lock_guard<std::mutex> guard(mutex_);
    // The checked native backend only tests shared PROFILE presence. It never
    // reads a payload or owns the AP provider, baseline, selection or admission.
    const bool before_root=state_==SessionState::prepared && !entered_;
    const bool before_publication=state_==SessionState::starting && qualified_ && startup_thread_==GetCurrentThreadId();
    if ((!before_root && !before_publication) || fault_ != SessionFault::none ||
        !root_ || !caller_ || routes_ != required_routes || !identity ||
        !expected_caller || native_route_caller != expected_caller) return false;
    if (!unrouted_.at_ms) unrouted_ = {GetTickCount64(), "presence_query", "query", state(), entered_, qualified_,
        0, 0, identity, native_route_caller, true, native_route_rva};
    return true;
}
void Session::unrouted_import(const char* route, const char* operation, uintptr_t provider, uintptr_t identity) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ == SessionState::prepared || state_ == SessionState::starting) {
        if (!unrouted_.at_ms || unrouted_.delegated_account_query) unrouted_ = {GetTickCount64(), route, operation, state(), entered_, qualified_,
            native_manager_, provider ? provider : provider_object_, identity ? identity : platform_identity_, native_route_caller, false, native_route_rva};
        installation.startup(2);
        if (fault_ == SessionFault::none) fault_ = SessionFault::missed_startup;
        state_ = SessionState::rejected; requests_ = false;
    }
}
bool Session::profile_read_completed() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!native_io() || !baseline_ready_ || !choice_ready_ || profile_failed_) return false;
    profile_finished_ = true;
    if (root_finished_) {
        state_ = SessionState::admitted; requests_ = !requests_stopped_;
        profile_step(ProfileStage::admission, ProfileStatus::succeeded, "profile_and_root_completed");
    } else profile_step(ProfileStage::admission, ProfileStatus::pending, "awaiting_root_return");
    return true;
}
bool Session::bind_provider(std::string_view root, uintptr_t provider, std::string_view ownership) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ != SessionState::starting || GetCurrentThreadId() != startup_thread_ || !provider ||
        root != native_root_ || ownership != ownership_) {
        if (fault_ == SessionFault::none) fault_ = SessionFault::provider_identity;
        state_ = routed() ? SessionState::faulted : SessionState::rejected;
        requests_ = false; return false;
    }
    provider_ = provider;
    ever_routed_.store(true, std::memory_order_release);
    state_.store(SessionState::binding, std::memory_order_release);
    requests_.store(false, std::memory_order_release);
    return true;
}
const char* native_campaign_prefix(unsigned index) {
    constexpr const char* prefixes[] = {"GAME-", "DLC1-", "DLC2-"};
    return index < 3 ? prefixes[index] : "";
}
int native_campaign_index(std::string_view prefix) {
    for (unsigned i = 0; i < 3; ++i) if (steam_name_equal(prefix, native_campaign_prefix(i))) return static_cast<int>(i);
    return -1;
}
bool Session::native_provider(uintptr_t& provider) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!native_io() || !provider_) return false;
    provider = provider_; return true;
}
bool Session::startup_provider_root(uintptr_t& root) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ != SessionState::starting || GetCurrentThreadId() != startup_thread_) return false;
    root = root_; return true;
}
bool Session::acquire_native_root_lock() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (state_ != SessionState::starting || GetCurrentThreadId() != startup_thread_) return false;
    if (native_root_lock_) return true;
    // This coordinates local game processes by the actual Steam root, including
    // configurations whose offline descriptor roots differ. It is not cloud quiescence.
    std::wstring name = L"Local\\SentinelCore-Steam-782330-";
    name.append(native_root_.begin(), native_root_.end());
    HANDLE lock = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!lock) return false;
    const DWORD result = WaitForSingleObject(lock, 0);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) { CloseHandle(lock); return false; }
    native_root_lock_ = lock; return true;
}
bool Session::provider_root(uintptr_t& root) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!routed() && (state_ != SessionState::starting || GetCurrentThreadId() != startup_thread_)) return false;
    root = root_; return true;
}
bool Session::observe_provider_objects(uintptr_t manager, uintptr_t control, uintptr_t object) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!manager || !control || !object) return false;
    if (state_ == SessionState::starting && GetCurrentThreadId() == startup_thread_ && !native_manager_) {
        native_manager_ = manager; provider_control_ = control; provider_object_ = object; return true;
    }
    return native_io() && manager == native_manager_ && control == provider_control_ && object == provider_object_;
}
bool Session::provider_operation(uintptr_t object, uintptr_t identity) {
    bool valid = false;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        valid = native_io() && object && object == provider_object_ && identity &&
            (!platform_identity_ || identity == platform_identity_);
        if (valid) platform_identity_ = identity;
    }
    if (!valid) fail(SessionFault::provider_identity);
    return valid;
}
void Session::provider_reset(uintptr_t manager) {
    bool affected = false;
    { std::lock_guard<std::mutex> guard(mutex_); affected = routed() && manager == native_manager_; }
    if (affected) fail(SessionFault::provider_identity);
}
void Session::stop_requests() {
    installation.startup(3);
    std::lock_guard<std::mutex> guard(mutex_);
    requests_stopped_ = true; requests_ = false;
    if (state_ == SessionState::prepared || state_ == SessionState::starting) {
        if (fault_ == SessionFault::none) fault_ = SessionFault::missed_startup;
        state_ = SessionState::rejected;
    }
}
void Session::fail(SessionFault why) { reject(why); }
namespace {
bool slot_name(std::string_view name) {
    constexpr std::string_view prefix = "AUTOSAVE";
    if (!steam_name_equal(name.substr(0, prefix.size()), prefix)) return false;
    const auto suffix = name.substr(prefix.size());
    return (suffix.size() == 1 && suffix[0] >= '0' && suffix[0] <= '9') || suffix == "10" || suffix == "11";
}
}
bool Session::publish_profile_catalog(uintptr_t provider, std::string_view ownership,
        std::vector<std::string> entries, std::string_view selected, int32_t index, bool prospective, unsigned campaign) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!routed() || provider != provider_ || ownership != ownership_ || profile_failed_ ||
        entries.size() > 12 || !slot_name(selected) || index < 0 || index >= 12 || campaign >= 3) return false;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!slot_name(entries[i])) return false;
        for (size_t j = 0; j < i; ++j) if (steam_name_equal(entries[i], entries[j])) return false;
        if (prospective && steam_name_equal(entries[i], selected)) return false;
    }
    if (prospective ? static_cast<size_t>(index) != entries.size() :
        (static_cast<size_t>(index) >= entries.size() || !steam_name_equal(entries[index], selected))) return false;
    ProfileChoice choice{};
    std::memcpy(choice.name.data(), selected.data(), selected.size()); choice.index = index;
    profile_catalog_ = std::move(entries); profile_choice_ = choice;
    prospective_choice_ = prospective; choice_ready_ = true;
    profile_campaign_ = campaign;
    return true;
}
bool Session::profile_choice(ProfileChoice& out) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!choice_ready_ || profile_failed_) return false;
    out = profile_choice_; return true;
}
bool Session::observe_profile_choice(std::string_view name, int32_t index) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!choice_ready_ || profile_failed_ || index < 0 || !slot_name(name)) return false;
    const bool listed = static_cast<size_t>(index) < profile_catalog_.size() && steam_name_equal(profile_catalog_[index], name);
    const bool prospective = prospective_choice_ && index == profile_choice_.index && steam_name_equal(profile_choice_.name.data(), name);
    if (!listed && !prospective) return false;
    profile_choice_ = {}; std::memcpy(profile_choice_.name.data(), name.data(), name.size());
    profile_choice_.index = index; prospective_choice_ = prospective;
    return true;
}
bool Session::capture_profile_write(std::string_view name, int32_t index, unsigned campaign, ProfileWrite& out) {
    std::lock_guard<std::mutex> guard(mutex_);
    // observe_profile_choice already validated the serialized pair; require the
    // same observation and campaign here, before anything asynchronous escapes.
    if (!native_io() || profile_failed_ || !choice_ready_ || campaign != profile_campaign_ ||
        index != profile_choice_.index || !steam_name_equal(name, profile_choice_.name.data()) ||
        profile_sequence_ == UINT64_MAX) return false;
    out = {profile_choice_, campaign, ++profile_sequence_}; return true;
}
bool Session::remember_profile_write(uintptr_t data, const ProfileWrite& write) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!native_io() || profile_failed_ || !data || !write.sequence || profile_writes_.size() >= 64) return false;
    return profile_writes_.emplace(data, write).second;
}
bool Session::take_profile_write(uintptr_t data, ProfileWrite& out) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto found = profile_writes_.find(data);
    if (found == profile_writes_.end()) return false;
    out = found->second; profile_writes_.erase(found); return true;
}
void Session::forget_save_data(uintptr_t data) {
    { std::lock_guard<std::mutex> guard(mutex_); profile_writes_.erase(data); }
    { std::lock_guard<std::mutex> guard(profile_trace_mutex_); if (data == profile_data_) profile_data_ = 0; }
    native_writes.invalidate_source(data);
}
bool Session::capture_profile_baseline(const ProfileOwner& owner, std::string name, int32_t index) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!native_io() || !choice_ready_ || profile_failed_ || !owner.profile || !owner.manager || !owner.shell ||
        owner.user == UINT32_MAX || index < 0 || index >= 12 ||
        (!name.empty() && !steam_name_equal(name, "AUTOSAVE") && !slot_name(name))) return false;
    // Only the initial native read can capture the vanilla pair. Later serializers
    // never recapture AP/default selection, even when native recreates the shell.
    if (baseline_ready_) return owner.profile == profile_owner_.profile && owner.manager == profile_owner_.manager &&
        owner.shell == profile_owner_.shell && owner.user == profile_owner_.user && name == vanilla_name_ && index == vanilla_index_;
    vanilla_name_ = std::move(name); vanilla_index_ = index; profile_owner_ = owner;
    baseline_ready_ = true; return true;
}
bool Session::profile_baseline(const ProfileOwner& owner, const char*& name, int32_t& index) {
    std::lock_guard<std::mutex> guard(mutex_);
    // 141492010 GetOrCreatePlayerProfile keys shells by PROFILE+8 (native user
    // handle); 1417e6b10/1417e7010 get/set that field. Allocation addresses are
    // transient. The adapter proves the current shell/backlink/callback domain.
    // Session is configured once; provider/user reset or faults close native_io.
    const bool valid = native_io() && baseline_ready_ && !profile_failed_ && owner.profile && owner.shell &&
        owner.manager == profile_owner_.manager && owner.user == profile_owner_.user;
    {
        std::lock_guard<std::mutex> trace_guard(profile_trace_mutex_);
        profile_trace_.ownership = {profile_owner_, owner, native_manager_, provider_control_, provider_object_, platform_identity_,
            baseline_ready_, native_io(), valid};
    }
    if (!valid) return false;
    name = vanilla_name_.c_str(); index = vanilla_index_; return true;
}
bool Session::profile_output_baseline(const char*& name, int32_t& index) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!native_io() || !baseline_ready_ || profile_failed_) return false;
    name = vanilla_name_.c_str(); index = vanilla_index_; return true;
}
void Session::fail_profile() {
    { std::lock_guard<std::mutex> guard(mutex_); profile_failed_ = true; }
    fail(SessionFault::native_profile);
}
sc_save_admission_snapshot Session::inspect() const {
    std::lock_guard<std::mutex> guard(mutex_);
    sc_save_admission_snapshot out{};
    static_assert(sizeof(out) == 160);
    static_assert(static_cast<uint32_t>(SessionState::faulted) == SC_SAVE_SESSION_FAULTED);
    static_assert(static_cast<uint32_t>(SessionState::binding) == SC_SAVE_SESSION_BINDING);
    out.size = sizeof(out); out.abi_version = SC_SAVE_ADMISSION_ABI_VERSION;
    out.state = static_cast<uint32_t>(state_.load()); out.fault = static_cast<uint32_t>(fault_.load());
    out.prepared_routes = routes_; out.required_routes = required_routes;
    out.flags = (routed() ? SC_SAVE_SESSION_ROUTED : 0u) |
        (requests_.load() ? SC_SAVE_SESSION_ACCEPTING : 0u) |
        (qualified_ ? SC_SAVE_SESSION_STARTUP_QUALIFIED : 0u);
    std::memcpy(out.namespace_id, namespace_id_.c_str(), namespace_id_.size() + 1);
    std::memcpy(out.native_root, native_root_.c_str(), native_root_.size() + 1);
    return out;
}
bool Session::collecting(uintptr_t provider, std::string_view root) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return native_io() && provider == provider_ && root == native_root_;
}
Session& session() {
    // Native trampolines and unresolved jobs can outlive Core inspection. This
    // process owner deliberately has no static-destruction/unload cleanup path.
    static Session* owner = new Session;
    return *owner;
}
} // namespace sentinel::save
