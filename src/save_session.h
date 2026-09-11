// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_storage.h"
#include "save_sdk_write.h"
#include "sentinel_save.h"
#include <atomic>
#include <array>
#include <mutex>
#include <vector>
#include <map>

namespace sentinel::engine { struct Memory; }

namespace sentinel::save {
enum class SessionState { disabled, prepared, starting, admitted, rejected, faulted, binding };
enum class SessionFault {
    none, descriptor, installation, incomplete_routes, startup_context,
    repeated_startup, missed_startup, provider_identity, foreign_collector,
    malformed_entry, native_collection, native_copy, unscoped_delete, delete_indeterminate,
    native_write, native_profile, native_read
};
enum Route : uint32_t {
    startup_route = 1, collector_route = 2, campaign_routes = 4,
    profile_route = 8, provider_identity_route = 16, metadata_route = 32
};
constexpr uint32_t required_routes = startup_route | collector_route | campaign_routes |
    profile_route | provider_identity_route | metadata_route;
struct ProfileChoice { std::array<char, 16> name{}; int32_t index = -1; };
struct ProfileWrite { ProfileChoice choice; unsigned campaign = 0; uint64_t sequence = 0; };
struct NativeCampaignCatalog { std::vector<std::string> slots; std::string selected; };
int native_campaign_index(std::string_view);
const char* native_campaign_prefix(unsigned);
// ISteamRemoteStorage014 folds file names to lowercase. Keep borrowed/native
// spelling intact while comparing the supported ASCII slot/path vocabulary.
bool steam_name_equal(std::string_view, std::string_view);

// One process/session owner. Configuration is consumed once, before hooks are
// published. Inspection shutdown cannot destroy an admitted lease or unroute jobs.
class Session final {
    friend class BackupJob;
public:
    NativeWrites native_writes;
    ~Session();
    storage::Result configure(const storage::Descriptor&, std::unique_ptr<storage::Namespace>);
    void reject(SessionFault);
    void install(uintptr_t root, uintptr_t startup_return, uint32_t routes);
    bool startup_enter(uintptr_t root, uintptr_t caller, uint32_t thread);
    void startup_leave(bool abnormal);
    void unrouted_import();
    bool profile_read_completed();
    // The provider adapter supplies bytes read from this exact remote root before
    // campaign consumers. An offline manifest never calls this admission method.
    bool bind_provider(std::string_view root, uintptr_t provider, std::string_view ownership);
    bool native_provider(uintptr_t& provider) const;
    bool startup_provider_root(uintptr_t& root) const;
    bool provider_root(uintptr_t& root) const;
    bool observe_provider_objects(uintptr_t manager, uintptr_t control, uintptr_t object);
    bool provider_operation(uintptr_t object, uintptr_t identity);
    void provider_reset(uintptr_t manager);
    bool acquire_native_root_lock();
    void stop_requests();
    void fail(SessionFault);
    sc_save_admission_snapshot inspect() const;
    bool routed() const { return ever_routed_.load(std::memory_order_acquire); }
    bool native_io() const {
        const auto current = state();
        return current == SessionState::binding || current == SessionState::admitted;
    }
    bool configured() const { return state_.load() != SessionState::disabled; }
    bool collecting(uintptr_t provider, std::string_view root) const;
    SessionState state() const { return state_.load(std::memory_order_acquire); }
    SessionFault fault() const { return fault_.load(std::memory_order_acquire); }
    bool accepts_requests() const { return requests_.load(std::memory_order_acquire); }
    const std::string& native_root() const { return native_root_; }
    const std::string& namespace_id() const { return namespace_id_; }
    const std::string& ownership_record() const { return ownership_; }
    // Supplied by the native owner from its actual ordered slot list. A numeric
    // suffix is not a list index. A prospective choice must explicitly name an
    // unused slot at the end of that list.
    bool publish_profile_catalog(uintptr_t provider, std::string_view ownership,
        std::vector<std::string> entries, std::string_view selected, int32_t index, bool prospective, unsigned campaign);
    bool profile_choice(ProfileChoice&) const;
    bool observe_profile_choice(std::string_view name, int32_t index);
    bool capture_profile_write(std::string_view name, int32_t index, unsigned campaign, ProfileWrite&);
    bool remember_profile_write(uintptr_t data, const ProfileWrite&);
    bool take_profile_write(uintptr_t data, ProfileWrite&);
    void forget_save_data(uintptr_t data);
    bool persist_profile_write(const ProfileWrite&, engine::Memory&);
    bool capture_profile_baseline(uintptr_t profile, uintptr_t manager, std::string name, int32_t index);
    bool profile_baseline(uintptr_t profile, uintptr_t manager, const char*& name, int32_t& index) const;
    void fail_profile();
private:
    mutable std::mutex mutex_;
    std::atomic<SessionState> state_{SessionState::disabled};
    std::atomic<SessionFault> fault_{SessionFault::none};
    std::atomic<bool> requests_{false}, ever_routed_{false};
    bool attempted_ = false, entered_ = false, qualified_ = false;
    bool root_finished_ = false, profile_finished_ = false, requests_stopped_ = false;
    uintptr_t root_ = 0, caller_ = 0, provider_ = 0;
    uintptr_t native_manager_ = 0, provider_control_ = 0, provider_object_ = 0, platform_identity_ = 0;
    uint32_t routes_ = 0, startup_thread_ = 0;
    std::string namespace_id_, native_root_, ownership_;
    std::unique_ptr<storage::Namespace> lease_;
    void* native_root_lock_ = nullptr;
    std::vector<std::string> profile_catalog_;
    ProfileChoice profile_choice_{};
    unsigned profile_campaign_ = 0;
    uint64_t profile_sequence_ = 0;
    std::map<uintptr_t, ProfileWrite> profile_writes_;
    std::mutex selection_mutex_; // Serializes remote record writes, independently of Session state.
    std::array<uint64_t, 3> persisted_selection_{};
    bool choice_ready_ = false, prospective_choice_ = false, baseline_ready_ = false, profile_failed_ = false;
    uintptr_t profile_ = 0, profile_manager_ = 0;
    std::string vanilla_name_; // Written once; borrowed JSON strings remain valid until process exit.
    int32_t vanilla_index_ = -1;
};
Session& session();
} // namespace sentinel::save
