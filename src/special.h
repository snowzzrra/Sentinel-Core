#pragma once
#include "sentinel_special.h"
#include "engine_observer.h"
#include <windows.h>
#include <cstdint>
#include <string>
#include "save_b_trace.h"

namespace sentinel::special {

bool valid(const sc_special_request&);
bool same(const sc_special_request&, const sc_special_request&);
sc_special_result initial(const sc_special_request&);

struct SnapshotFacts {
    uint8_t native_crucible = 0;
    uint8_t native_hammer = 0;
    uint8_t native_hammer_perks = 0;   /* 0..2 permanent upgrade perks present */
    uint8_t native_selected = SC_SPECIAL_WEAPON_NONE;
    uint32_t known = 0;                /* SC_SPECIAL_KNOWN_* */
    uint32_t crucible_charge = 0;
    uint32_t crucible_charge_max = 0;
    uintptr_t held_weapon_decl = 0;
    bool selection_policy = false;
};

// Internal production seam; never externally supplied or serialized.
struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void* context) = nullptr;
    bool (*read)(void* context, uintptr_t player_ptr, SnapshotFacts& facts) = nullptr;
    uint32_t (*ensure)(void* context, uintptr_t player_ptr, uint32_t own_crucible, uint32_t own_hammer, uint32_t hammer_tier) = nullptr;
    uint32_t (*select)(void* context, uintptr_t player_ptr, uint32_t selected) = nullptr;
    uint32_t (*refill)(void* context, uintptr_t player_ptr) = nullptr;
    bool (*present)(void* context, uintptr_t player_ptr, uint32_t balance, uint32_t flags, uint32_t used) = nullptr;
    void (*bind_run_state)(void* context, uintptr_t player_ptr, uint32_t own_crucible, uint32_t own_hammer,
                           uint32_t hammer_tier, uint32_t selected) = nullptr;
};

// Pure acquisition-preservation decision for the native Special path.
// `before_decl`/`before_item` describe the weapon actually in hands immediately
// before a physical acquisition (item zero when it does not belong to the
// current inventory); `after_decl` is the weapon in hands afterwards. The
// preferred Special selection is a separate concept and is never derived here.
enum class AcquisitionRestore { none, equip_prior, fail_closed };
AcquisitionRestore acquisition_restore(uintptr_t before_decl, uintptr_t before_item, uintptr_t after_decl);

// Called by the native input seam on the validated game thread. Creates one
// pending use-request at most; a press while a live request is pending is
// coalesced and never queued twice.
bool create_refill_request(uint64_t now_ms);
// Uses the same fresh ownership, selection operation and verified preference as IPC.
sc_special_result toggle_local(const char* namespace_id, const Calls&);

extern Calls calls;

void execute(const sc_special_request&, sc_special_result&, const Calls&);
void install(const engine::Binding&, HANDLE stop);
bool available();
bool selection_route_available();
save::BSnapshot installation_diagnostics();
save::BSnapshot input_diagnostics();
save::BSnapshot route_diagnostics();
save::BSnapshot hud_diagnostics();
struct UseHistory {
    uint64_t sequence = 0, overwritten = 0, lock_dropped = 0;
    uint64_t attempt = 0, attempt_started = 0, idle_queries = 0, coalesced = 0;
    bool attempt_active = false;
    uint32_t count = 0;
    save::BEvent first_failure{};
    std::array<save::BEvent, 32> events{};
};
UseHistory use_history();
// Cumulative owner state and selection policy, independent of native presence/resources.
struct HudOwnerSnapshot {
    uint64_t revision = 0, request_revision = 0;
    uint32_t owns_crucible = 0, owns_hammer = 0, selected = 0;
    uint32_t refill_balance = UINT32_MAX, refill_flags = 0;
    uint32_t refill_request_state = SC_SPECIAL_REFILL_IDLE;
    bool refill_enabled = false, namespace_valid = false;
};
HudOwnerSnapshot hud_owner_snapshot(const char (&namespace_id)[65]);
std::wstring input_directory();
bool admitted(const char* namespace_id);
void execute_native(const sc_special_request&, sc_special_result&);
void bind_run_state_if_needed(uintptr_t player, uint64_t generation = 0);
void refresh_input_config();
void poll_input(uintptr_t player, bool safe_gameplay);
void reset_session(const char* namespace_id);

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls, const char* namespace_id);
#endif
}
