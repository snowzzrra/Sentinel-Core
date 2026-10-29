#pragma once
#include "weapon_points.h"
#include "inventory.h"
#include "arsenal.h"
#include "runes.h"
#include "special.h"
#include "deathlink.h"
#include "commands.h"
#include "sentinel_native.h"
#include "sentinel_save_request.h"
#include "save_backup.h"
#include "save_submission.h"
#include <array>
#include <atomic>
#include <map>
#include "command_registry.h"

namespace sentinel::native {
bool same_scope(const sc_native_scope& a, const sc_native_scope& b);

// the caller serializes admission and history; native calls run without that lock
struct Lifecycle {
    uint64_t generation = 0, sequence = 0, overwritten = 0;
    uint32_t state = SC_LIFETIME_UNOBSERVED, depth = 0;
    uint32_t checkpoint_known = 0, checkpoint = 0;
    std::array<sc_native_event, SC_NATIVE_HISTORY> history{};
    void event(uint32_t kind, uint64_t now, uint32_t tid);
    void begin(bool change, bool flag_known, bool flag, uint64_t now, uint32_t tid);
    void end(bool change, bool success, uint32_t game_state, uint64_t now, uint32_t tid);
    void page(sc_native_snapshot& out, uint64_t after) const;
};

class Diagnostics {
    struct Identity;
public:
    struct Slot;
    enum class Error { none, duplicate_mismatch, identity_capacity, unsupported_revision, not_found, malformed, not_terminal, token_mismatch };
    enum class Retention { full_result, compact_terminal, not_found };
    struct Reply {
        Error error = Error::none;
        generated::command_reference reference{};
        sc_diagnostic_result execution{};
        sc_diagnostic_detail detail{};
        generated::NativeResult value{};
        registry::Evidence evidence{};
        std::array<uint8_t, 16> result_token{};
        Retention retention = Retention::not_found;
    };
    Reply submit(const generated::submission&, const generated::NativeRequest&, uint32_t reject, uint64_t now,
        const registry::Handler* handler = nullptr, const registry::LiveState* live = nullptr);
    Reply query(const generated::command_reference&, bool cancel, uint64_t now);
    Reply ack(const generated::command_reference&, const std::array<uint8_t, 16>& token, uint64_t now);
    Slot* transient(const sc_diagnostic_request&, const generated::NativeRequest&, uint32_t, uint64_t);
    bool collect_transient(Slot&, Reply&, uint64_t);
    size_t identity_count() const { return identities_.size(); }

    struct Slot {
        std::atomic<uint32_t> state{SC_DIAGNOSTIC_UNKNOWN};
        std::atomic<bool> cancel{false}, entered{false};
        sc_diagnostic_request request{};
        sc_diagnostic_result result{};
        sc_diagnostic_detail detail{};
        uint64_t admitted_lock_misses = 0;
        Identity* identity = nullptr;
        bool transient_read = false;
        generated::CommandId command_type = generated::CommandId::diagnostic;
        std::array<uint8_t, 16> save_binding{};
        registry::Evidence evidence{};
        generated::NativeRequest payload{};
        generated::NativeResult value{};
        template<class T> bool is() const { return std::holds_alternative<T>(payload); }
        template<class T> const T& get() const { return std::get<T>(payload); }
        template<class T> T& output() { return std::get<T>(value); }
        std::shared_ptr<save::BackupJob> backup;
        save::SubmissionResult submission;
        // the callback publishes its final submission facts here, then stops accessing the slot. queue housekeeping owns it after this handoff
        std::atomic<bool> awaiting_backup{false};
    };
    // admission/retrieval/claim/housekeeping run under the caller's short lock
    sc_diagnostic_result submit(const sc_diagnostic_request& request,
                                uint32_t reject, uint64_t now, sc_diagnostic_detail* detail = nullptr,
                                const sc_save_backup_request* backup = nullptr,
                                const sc_weapon_points_request* points = nullptr,
                                const sc_inventory_request* inventory = nullptr,
                                const sc_arsenal_request* arsenal = nullptr,
                                const sc_runes_request* runes = nullptr,
                                const sc_special_request* special = nullptr,
                                const sc_deathlink_request* deathlink = nullptr,
                                const sc_command_request* command = nullptr);
    sc_diagnostic_result retrieve(const sc_diagnostic_request& request, bool cancel, uint64_t now,
                                  sc_diagnostic_detail* detail = nullptr,
                                  const sc_save_backup_request* backup = nullptr,
                                  const sc_weapon_points_request* points = nullptr,
                                  const sc_inventory_request* inventory = nullptr,
                                  const sc_arsenal_request* arsenal = nullptr,
                                  const sc_runes_request* runes = nullptr,
                                  const sc_special_request* special = nullptr,
                                  const sc_deathlink_request* deathlink = nullptr,
                                  const sc_command_request* command = nullptr);
    sc_weapon_points_result points_result(const sc_weapon_points_request&, bool cancel, uint64_t now, bool release = false);
    sc_inventory_result inventory_result(const sc_inventory_request&, bool cancel, uint64_t now, bool release = false);
    sc_arsenal_result arsenal_result(const sc_arsenal_request&, bool cancel, uint64_t now, bool release = false);
    sc_runes_result runes_result(const sc_runes_request&, bool cancel, uint64_t now, bool release = false);
    sc_special_result special_result(const sc_special_request&, bool cancel, uint64_t now, bool release = false);
    sc_deathlink_result deathlink_result(const sc_deathlink_request&, bool cancel, uint64_t now, bool release = false);
    sc_command_result command_result(const sc_command_request&, bool cancel, uint64_t now, bool release = false);
    sc_save_backup_snapshot backup_result(const sc_save_backup_request&, bool cancel, uint64_t now);
    static void await_backup(Slot&, sc_diagnostic_result, sc_diagnostic_detail, save::SubmissionResult);
    void note_claim_contention() { claim_lock_misses_.fetch_add(1, std::memory_order_relaxed); }
    Slot* claim(uint64_t now);
    Slot* claim(const generated::command_reference&, uint64_t now);
    void cancel_pending(uint64_t now);
    void counts(sc_native_snapshot& out, uint64_t now);
    // claimed work belongs to the callback until terminal publication or backup handoff
    static void finish(Slot& slot, sc_diagnostic_result result, sc_diagnostic_detail detail = {});
private:
    struct Identity {
        generated::command_reference reference{};
        Slot* slot = nullptr;
        uint32_t state = SC_DIAGNOSTIC_UNKNOWN, reason = 0;
        registry::Evidence evidence{};
        std::array<uint8_t, 16> token{};
    };
    using IdentityKey = std::pair<std::array<uint8_t, 16>, std::string>;
    std::map<IdentityKey, Identity> identities_;
    Slot* allocate(const sc_diagnostic_request&, const generated::NativeRequest&, uint64_t now);
    Identity* reserve(const generated::submission&, const std::array<uint8_t, 32>& digest);
    Reply admit_identity(Identity&, const generated::submission&, const generated::NativeRequest&, uint32_t reject, uint64_t now,
        const registry::Handler*, const registry::LiveState*);
    static bool matches_legacy(const Slot&, const sc_diagnostic_request&, const generated::NativeRequest&);
    void request_cancel(Slot&, uint64_t now);
    static void complete_backup(Slot&, uint64_t now);
    static void retire(Slot&);
    static void archive(Slot&);
    static Reply reply(const Identity&, uint64_t now);
    std::atomic<uint64_t> claim_lock_misses_{0};
    void queue_detail(Slot& slot);
    std::array<Slot, SC_DIAGNOSTIC_CAPACITY> slots_{};
    void collect(uint64_t now);
};
}
