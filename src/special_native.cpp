#include "engine/builds/build_descriptor.h"
#include "special.h"
#include "player_strings_generated.h"
#include "native_target.h"
#include "native_runtime.h"
#include "local_controls.h"
#include "ap_controls_native.h"
#include "combat_presentation.h"
#include "save_session.h"
#include "inventory.h"
#include "MinHook.h"
#include "engine/hooks/hook_set.h"
#include "hde/hde64.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <intrin.h>
#include <cmath>
#include <mutex>
#include <type_traits>
#include <chrono>
#ifdef SC_NATIVE_TESTING
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#endif

namespace sentinel::special {
namespace {
hooks::HookSet selection_hooks("special_selection"), hands_hooks("special_hands"), loot_hooks("special_loot"),
    toast_hooks("special_toast"), fast_travel_hooks("special_fast_travel"), hud_hooks("special_hud"), chainsaw_hooks("special_chainsaw");

std::atomic<bool> ready{false};
save::BTrace installation_trace, input_trace, route_trace, hud_trace;
SRWLOCK refill_lock = SRWLOCK_INIT;
RefillHistory refill_events;

void record_refill(const RefillPool& pool) {
    AcquireSRWLockExclusive(&refill_lock);
    refill_events.pools[refill_events.sequence++ % refill_events.pools.size()] = pool;
    if (refill_events.count < refill_events.pools.size()) ++refill_events.count;
    ReleaseSRWLockExclusive(&refill_lock);
}
void refill_name(char (&out)[192], const char* name) noexcept {
    std::memset(out, 0, sizeof(out));
    __try { if (name) for (size_t i = 0; i < sizeof(out) - 1 && name[i]; ++i) out[i] = name[i]; } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
SRWLOCK use_history_lock = SRWLOCK_INIT;
UseHistory use_events;
std::atomic<uint64_t> use_lock_dropped{0};
std::atomic<uint64_t> use_attempt_started{0};

void begin_use_attempt() {
    if (!TryAcquireSRWLockExclusive(&use_history_lock)) {
        ++use_lock_dropped;
        return;
    }
    ++use_events.attempt;
    use_events.attempt_started = GetTickCount64();
    use_attempt_started = use_events.attempt_started;
    use_events.attempt_active = true;
    use_events.first_failure = {};
    ReleaseSRWLockExclusive(&use_history_lock);
}
bool coalesce_use_event(const save::BEvent& event) {
    // skip repeat logs from the same native caller until its facts change
    for (uint32_t i = use_events.count; i; --i) {
        const auto& prior = use_events.events[(use_events.sequence - use_events.count + i - 1) % use_events.events.size()];
        if (prior.operation != event.operation || prior.predicate != event.predicate ||
            !prior.facts[0].key || prior.facts[0].value != event.facts[0].value) continue;
        bool equal = prior.status == event.status;
        for (size_t j = 0; equal && j < event.facts.size(); ++j)
            equal = prior.facts[j].key == event.facts[j].key && prior.facts[j].value == event.facts[j].value;
        if (equal) return true;
        break;
    }
    return false;
}
void record_use(const char* predicate, save::BStatus status, std::initializer_list<save::BFact> facts) {
    if (!TryAcquireSRWLockExclusive(&use_history_lock)) {
        use_lock_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    save::BEvent event{};
    event.stage = save::BStage::special_input;
    event.status = status;
    event.predicate = predicate;
    event.thread = GetCurrentThreadId();
    event.operation = use_events.attempt;
    size_t count = 0;
    for (const auto& fact : facts) {
        if (count == event.facts.size()) break;
        event.facts[count++] = fact;
    }
    const bool query = !std::strcmp(predicate, "crucible_resolver") || !std::strcmp(predicate, "hammer_input") || !std::strcmp(predicate, "input_job_scope") ||
        !std::strcmp(predicate, "weapon_dispatch_scope") || !std::strcmp(predicate, "crucible_input_edge");
    if (query && !use_events.attempt_active) {
        ++use_events.idle_queries;
        ReleaseSRWLockExclusive(&use_history_lock);
        return;
    }
    if (coalesce_use_event(event)) {
        ++use_events.coalesced;
        ReleaseSRWLockExclusive(&use_history_lock);
        return;
    }
    event.sequence = ++use_events.sequence;
    event.at_ms = GetTickCount64();
    if (use_events.count < use_events.events.size()) ++use_events.count;
    else ++use_events.overwritten;
    use_events.events[(event.sequence - 1) % use_events.events.size()] = event;
    if (status == save::BStatus::refused && !use_events.first_failure.sequence) use_events.first_failure = event;
    ReleaseSRWLockExclusive(&use_history_lock);
}
SRWLOCK directory_lock = SRWLOCK_INIT;
std::wstring controls_directory;
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;

#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif

// checked against steam image 9809708c; all addresses are offsets from the image base
const auto rva_inventory_typeinfo = builds::steam_20260818().special.rva_inventory_typeinfo;
const auto rva_find_decl = builds::steam_20260818().special.rva_find_decl;
const auto rva_find_item = builds::steam_20260818().special.rva_find_item;
const auto rva_give_item = builds::steam_20260818().special.rva_give_item;
const auto rva_item_count = builds::steam_20260818().special.rva_item_count;
const auto rva_item_at = builds::steam_20260818().special.rva_item_at;
const auto rva_meter_set = builds::steam_20260818().special.rva_meter_set;
const auto rva_judgement_meter_vtable = builds::steam_20260818().special.rva_judgement_meter_vtable;
const auto judgement_meter_offset = builds::steam_20260818().special.judgement_meter_offset;
const auto rva_unlock_perk = builds::steam_20260818().special.rva_unlock_perk;
const auto rva_activate_perk = builds::steam_20260818().special.rva_activate_perk;
const auto rva_active_perk = builds::steam_20260818().special.rva_active_perk;
const auto rva_loot_spawn_amount = builds::steam_20260818().special.rva_loot_spawn_amount;
const auto rva_perk_typeinfo = builds::steam_20260818().special.rva_perk_typeinfo;
const auto rva_current_weapon = builds::steam_20260818().special.rva_current_weapon;
const auto rva_hud_earnings = builds::steam_20260818().special.rva_hud_earnings;
const auto rva_hud_element_setup = builds::steam_20260818().special.rva_hud_element_setup;
const auto rva_mission_challenge_vtable = builds::steam_20260818().special.rva_mission_challenge_vtable;
const auto rva_weapon_info_vtable = builds::steam_20260818().special.rva_weapon_info_vtable;
const auto rva_fast_travel_checkpoint_render = builds::steam_20260818().special.rva_fast_travel_checkpoint_render;
const auto rva_fast_travel_widget_resolve = builds::steam_20260818().special.rva_fast_travel_widget_resolve;
const auto rva_player = builds::steam_20260818().special.rva_player;
const auto rva_crucible_resolver = builds::steam_20260818().special.rva_crucible_resolver;
const auto rva_input_down = builds::steam_20260818().special.rva_input_down;
const auto rva_hammer_input_return = builds::steam_20260818().special.rva_hammer_input_return;
const auto rva_weapon_cast = builds::steam_20260818().special.rva_weapon_cast;
const auto rva_best_weapon = builds::steam_20260818().special.rva_best_weapon;
const auto rva_weapon_mode_decl = builds::steam_20260818().special.rva_weapon_mode_decl;
const auto rva_hands_reset_return = builds::steam_20260818().special.rva_hands_reset_return;
const auto rva_hands_refresh_return = builds::steam_20260818().special.rva_hands_refresh_return;
const auto rva_hands_init_return = builds::steam_20260818().special.rva_hands_init_return;
using BestWeapon = uintptr_t(*)(uintptr_t, bool, uintptr_t);
BestWeapon original_best_weapon = nullptr;
constexpr uint64_t special_button = UINT64_C(0x400000000);
using CrucibleResolver = bool(*)(uintptr_t);
// the checked leaf returns setnz al; its callers test al, not rax
using InputDown = bool(*)(uintptr_t, uint64_t);
CrucibleResolver original_crucible_resolver = nullptr;
InputDown original_input_down = nullptr;
std::atomic<bool> route_ready{false};
std::atomic<bool> native_upgrade_ready{false};
std::atomic<DWORD> route_thread{0};
std::atomic<uintptr_t> route_player{0};
char route_namespace[65]{};
SRWLOCK route_namespace_lock = SRWLOCK_INIT;
std::atomic<uint64_t> route_epoch{0};
std::atomic<uint32_t> route_requested{SC_SPECIAL_WEAPON_NONE};
struct PolicySnapshot {
    uintptr_t player = 0;
    uint64_t epoch = 0;
    uint32_t selected = 0;
    bool crucible = false, hammer = false, known = false;
    char namespace_id[65]{};
};
PolicySnapshot published_policy; // protected with route_namespace_lock
std::atomic<bool> special_down{false};
thread_local uintptr_t input_job_player = 0;
thread_local uint64_t input_job_epoch = 0;
using InputJob = void(*)(uintptr_t);
InputJob original_input_job = nullptr;
using HammerAttack = void(*)(uintptr_t);
HammerAttack original_hammer_attack = nullptr;
using WeaponDispatch = void(*)(uintptr_t, uintptr_t);
WeaponDispatch original_weapon_dispatch = nullptr;
using CrucibleActivate = bool(*)(uintptr_t);
CrucibleActivate original_crucible_activate = nullptr;
InputDown original_input_pressed = nullptr;
using ChainsawTick = void(*)(uintptr_t);
ChainsawTick original_chainsaw_tick = nullptr;
uint32_t route_scope_reason(uintptr_t p);
bool stale_chainsaw_inhibit(uint8_t flags, uint32_t controls) { return (flags & 0x40) && controls == 0; }
void chainsaw_tick_detour(uintptr_t p) {
    static std::atomic<uint64_t> chainsaw_calls{0},next{0},checked_epoch{0};
    ++chainsaw_calls;
    const auto now=GetTickCount64();
    __try {
        const auto epoch=native::observation_stamp();
        const auto controls=p ? *reinterpret_cast<uint32_t*>(p+0x87fc) : UINT32_MAX;
        if (controls) checked_epoch.store(0,std::memory_order_release);
        else if (p && epoch && checked_epoch.load(std::memory_order_acquire)!=epoch && !route_scope_reason(p) && checked_epoch.exchange(epoch)!=epoch) {
            // reconcile after loading or control release; later scripted chainsaw blocks stay intact.
            if (stale_chainsaw_inhibit(*reinterpret_cast<uint8_t*>(p+0x7370),controls)) {
                *reinterpret_cast<uint8_t*>(p+0x7370) &= ~uint8_t{0x40};
                input_trace.record(save::BStage::chainsaw_state,save::BStatus::succeeded, "chainsaw_stale_inhibit_released",0,{{"player",p}});
            }
        }
        auto due=next.load();
        if (p && now>=due && next.compare_exchange_strong(due,now+1000)) {
            input_trace.record(save::BStage::chainsaw_state,save::BStatus::entered,"chainsaw_native_entry",0,
                {{"calls",chainsaw_calls.load()},{"player",p},{"pending_frame",*reinterpret_cast<uint32_t*>(p+0x7434)},
                 {"flags_87fc",*reinterpret_cast<uint32_t*>(p+0x87fc)},
                 {"flags_7370",*reinterpret_cast<uint8_t*>(p+0x7370)},
                 {"flags_1606a",*reinterpret_cast<uint8_t*>(p+0x1606a)},
                 {"flags_1606b",*reinterpret_cast<uint8_t*>(p+0x1606b)},
                 {"flags_1606e",*reinterpret_cast<uint8_t*>(p+0x1606e)}});
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    original_chainsaw_tick(p);
}
using FastTravelCheckpointRender = void(*)(uintptr_t, int);
FastTravelCheckpointRender original_fast_travel_checkpoint_render = nullptr;

// shortcut: MSVC 14.44 breaks optimized SEH here; retest on compiler upgrade.
#pragma optimize("", off)
__declspec(noinline) bool fast_travel_widget_ready(uintptr_t screen) {
    if (!screen) return false;
    __try {
        const auto widget = *reinterpret_cast<uintptr_t*>(screen + 0x108);
        if (!widget) return false;
        if (!*reinterpret_cast<uintptr_t*>(widget + 0x18)) {
            const auto resolve = reinterpret_cast<bool(*)(uintptr_t)>(image_base + rva_fast_travel_widget_resolve);
            if (!resolve(widget)) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ready.store(false, std::memory_order_release);
        installation_trace.record(save::BStage::native_start, save::BStatus::refused, "fast_travel_widget_fault", 0, {{"exception", GetExceptionCode()}});
        return false;
    }
}
#pragma optimize("", on)
void fast_travel_checkpoint_render_detour(uintptr_t screen, int index) {
    if (original_fast_travel_checkpoint_render && fast_travel_widget_ready(screen)) original_fast_travel_checkpoint_render(screen, index);
}

const auto rva_equipment_upgrade_vtable = builds::steam_20260818().special.rva_equipment_upgrade_vtable;
const auto perk_component_offset = builds::steam_20260818().special.perk_component_offset;
const auto equipment_upgrade_offset = builds::steam_20260818().special.equipment_upgrade_offset;
const auto hammer_loot_modifier_offset = builds::steam_20260818().special.hammer_loot_modifier_offset;
using LootSpawnAmount = uint64_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, float*, float*, uint32_t, uint32_t, uint32_t);
LootSpawnAmount original_loot_spawn_amount = nullptr;
bool hammer_loot_modifier(uintptr_t p, bool& effective);

uint64_t loot_spawn_amount_detour(uintptr_t loot, uintptr_t declaration, uintptr_t player, uintptr_t target, float* amount, float* scale,
                                  uint32_t arg7, uint32_t trigger, uint32_t mode) {
    const auto result = original_loot_spawn_amount( loot, declaration, player, target, amount, scale, arg7, trigger, mode);
    if (trigger >= 0x18 && trigger <= 0x1a && player && player == route_player.load(std::memory_order_relaxed)) {
        bool modifier = false;
        const bool qualified = hammer_loot_modifier(player, modifier);
        record_use("hammer_loot_amount", result ? save::BStatus::succeeded : save::BStatus::refused,
                   {{"trigger", trigger}, {"amount", result}, {"arg7", arg7},
                    {"target", target != 0}, {"modifier_known", qualified}, {"modifier", modifier}});
    }
    return result;
}
const char* const CRUCIBLE_PATH = "weapon/player/crucible";
const char* const HAMMER_PATH = "weapon/player/hammer";
const char* const CRUCIBLE_AMMO_PATH = "ammo/sharedammopool/crucible";
const char* const HAMMER_PERK_PATHS[3] = {
    "perk/player/weapons/hammer/ammo_drops_upgraded", "perk/player/weapons/hammer/armor_and_health_drops_upgraded",
    "perk/player/weapons/hammer/debuff_duration_increase"
};

using FindDecl = uintptr_t(*)(uintptr_t, const char*, int);
using FindItem = uintptr_t(*)(uintptr_t, uintptr_t);
using GiveItem = uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, int, uint8_t, uint8_t, uint8_t, uint8_t);
using ItemCount = uint32_t(*)(uintptr_t);
using ItemAt = uintptr_t(*)(uintptr_t, int);
using UnlockPerk = void(*)(uintptr_t, uintptr_t, uint8_t, uint8_t, uintptr_t, uint8_t);
using ActivatePerk = void(*)(uintptr_t, uintptr_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
using ActivePerk = uint8_t(*)(uintptr_t, uintptr_t);
using CurrentWeapon = uintptr_t(*)(uintptr_t);
using EarningsAppend = void(*)(uintptr_t, const char*, const char*, uint32_t, uint64_t, uint32_t);
using HudElementSetup = char(*)(uintptr_t);

HudElementSetup original_hud_element_setup = nullptr;
using WeaponHudUpdate = void(*)(uintptr_t, uintptr_t);
using WeaponHudProject = void(*)(uintptr_t);
WeaponHudUpdate original_weapon_hud_update = nullptr;
WeaponHudProject project_crucible_hud = nullptr, project_hammer_hud = nullptr;
using WeaponInfoLabel = uint64_t(*)(uintptr_t, const char*, bool);
WeaponInfoLabel original_weapon_info_label = nullptr;
std::atomic<unsigned> configured_keys{0};
// the engine owns all clips
namespace hud {
struct Value { uint32_t type, reserved; uintptr_t payload; };
struct alignas(8) String { unsigned char storage[0x30]; };
struct Calls {
    Value* (*lookup)(uintptr_t, Value*, const char*) = nullptr;
    uintptr_t (*sprite)(const Value*) = nullptr;
    uintptr_t (*text)(const Value*) = nullptr;
    void (*release)(Value*) = nullptr;
    void (*string_init)(String*) = nullptr;
    void (*string_set)(String*, const char*) = nullptr;
    void (*string_free)(String*) = nullptr;
    uintptr_t (*duplicate)(uintptr_t, const String*) = nullptr;
    uintptr_t (*entry)(uintptr_t, int) = nullptr;
    uintptr_t (*add)(uintptr_t, int, unsigned, const String*) = nullptr;
    void (*dirty)(uintptr_t) = nullptr;
    void (*start)(uintptr_t, int) = nullptr;
    void (*frame)(uintptr_t, int) = nullptr;
    uint64_t (*label)(uintptr_t, const char*, bool) = nullptr;
    uintptr_t (*keycap)(uintptr_t) = nullptr;
    void (*visible)(uintptr_t, bool, bool) = nullptr;
    void (*set_text)(uintptr_t, const char*) = nullptr;
    void (*position)(uintptr_t, float, float) = nullptr;
    void (*color)(uintptr_t, int) = nullptr;
    void (*material)(uintptr_t, uintptr_t, int, int, unsigned) = nullptr;
    uintptr_t (*find_material)(uintptr_t, const char*, int) = nullptr;
} swf;
bool graphics_ready = false, keycap_ready = false;

void refuse(const char* reason, uintptr_t owner, uintptr_t element, uint64_t epoch) {
    hud_trace.record(save::BStage::profile_output, save::BStatus::refused, reason, 0, {{"owner", owner}, {"element", element}, {"generation", epoch}});
}
uintptr_t child(uintptr_t parent, const char* name) {
    if (!parent) return 0;
    Value value{};
    const auto result = swf.lookup(parent, &value, name);
    const auto object = swf.sprite(result);
    if (value.type == 2 || value.type == 8) swf.release(&value);
    return object;
}
uintptr_t text_child(uintptr_t parent, const char* name) {
    if (!parent) return 0;
    Value value{};
    const auto result = swf.lookup(parent, &value, name);
    const auto object = swf.text(result);
    if (value.type == 2 || value.type == 8) swf.release(&value);
    return object;
}

// idSWFTextInstance::text is idStr at +0x40 (the native setter passes this
// address to idStr::operator=). Its data and length are at +0x8/+0x10.
bool text_is(uintptr_t value, const char* label) {
    const auto length = std::strlen(label);
    const auto data = *reinterpret_cast<const char* const*>(value + 0x48);
    return *reinterpret_cast<int32_t*>(value + 0x50) == static_cast<int32_t>(length) && data && std::memcmp(data, label, length) == 0;
}

struct Point { float x, y; };
int64_t trace_point(Point value) {
    static_assert(sizeof(Point) == sizeof(uint64_t));
    uint64_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<int64_t>(bits);
}
bool position(uintptr_t sprite, Point& out) {
    if (!sprite || !*reinterpret_cast<uintptr_t*>(sprite + 0x40)) return false;
    const auto slot = *reinterpret_cast<int32_t*>(sprite + 0xc);
    const auto context = *reinterpret_cast<uintptr_t*>(sprite + 0x10);
    if (slot < 0 || !context) return false;
    const auto transforms = *reinterpret_cast<uintptr_t*>(context + 0x80);
    if (!transforms) return false;
    const auto transform = transforms + static_cast<uintptr_t>(slot) * 0x40;
    out = {*reinterpret_cast<float*>(transform + 0x14), *reinterpret_cast<float*>(transform + 0x18)};
    return std::isfinite(out.x) && std::isfinite(out.y);
}
void show(uintptr_t clip, bool visible) { if (*reinterpret_cast<uint8_t*>(clip + 0x51) != static_cast<uint8_t>(visible)) swf.visible(clip, visible, true); }

void place(uintptr_t clip, Point at) {
    Point current{};
    if (!position(clip, current) || current.x != at.x || current.y != at.y) swf.position(clip, at.x, at.y);
}
bool hold_frame(uintptr_t clip, uint16_t frame) {
    if (*reinterpret_cast<uint16_t*>(clip + 0x58) != frame || *reinterpret_cast<uint8_t*>(clip + 0x50) || *reinterpret_cast<uint16_t*>(clip + 0x5c)) {
        swf.frame(clip, frame);
        return true;
    }
    return false;
}
bool hold_ice_frame(uintptr_t parent, bool cursed) {
    const auto frame = swf.label(parent, cursed ? "cursed" : "normal", false);
    const auto empty = swf.label(parent, "noEquipment", false);
    return frame > 0 && frame != empty && hold_frame(parent, static_cast<uint16_t>(frame));
}
bool key_offset(uintptr_t clip, uintptr_t source, uintptr_t movie, Point& out) {
    // the 0x40 transform slot contains swfmatrix_t after its 4-byte header: xx, yy, xy, yx, tx, ty. swf.position writes tx/ty at +0x14/+0x18
    Point point{};
    for (unsigned i = 0; i < 4 && clip; ++i) {
        if (*reinterpret_cast<uintptr_t*>(clip + 0x30) != movie) return false;
        const auto slot = *reinterpret_cast<int32_t*>(clip + 0xc);
        const auto context = *reinterpret_cast<uintptr_t*>(clip + 0x10);
        if (slot < 0 || !context) return false;
        const auto transforms = *reinterpret_cast<uintptr_t*>(context + 0x80);
        if (!transforms) return false;
        const auto t = transforms + static_cast<uintptr_t>(slot) * 0x40;
        const Point next{
            *reinterpret_cast<float*>(t + 0x4) * point.x + *reinterpret_cast<float*>(t + 0xc) * point.y + *reinterpret_cast<float*>(t + 0x14),
            *reinterpret_cast<float*>(t + 0x10) * point.x + *reinterpret_cast<float*>(t + 0x8) * point.y + *reinterpret_cast<float*>(t + 0x18)};
        if (!std::isfinite(next.x) || !std::isfinite(next.y)) return false;
        point = next;
        if (clip == source) {
            Point anchor{};
            if (!position(source, anchor)) return false;
            out = {point.x - anchor.x, point.y - anchor.y};
            return true;
        }
        clip = *reinterpret_cast<uintptr_t*>(clip + 0x40);
    }
    return false;
}

struct Rect { Point tl, br; };
bool raw_bounds(uintptr_t clip, Rect& out) {
    if (!clip) return false;
    out = {*reinterpret_cast<Point*>(clip + 0xa8), *reinterpret_cast<Point*>(clip + 0xb0)};
    return std::isfinite(out.tl.x) && std::isfinite(out.tl.y) && std::isfinite(out.br.x) && std::isfinite(out.br.y) &&
        out.br.x > out.tl.x && out.br.y > out.tl.y;
}
Point center(Rect rect) { return {(rect.tl.x + rect.br.x) * 0.5f, (rect.tl.y + rect.br.y) * 0.5f}; }

bool unrender(uintptr_t clip, Point point, Point& out) {
    // rendersprite (0x18341d0) stores the rendered bounds at +0xa8 and their local-to-render matrix at +0x90. both describe the same render
    const auto m = reinterpret_cast<const float*>(clip + 0x90);
    const float det = m[0] * m[1] - m[2] * m[3];
    const float area = std::fabs(m[0] * m[1]) + std::fabs(m[2] * m[3]);
    if (!std::isfinite(det) || std::fabs(det) <= area * 0.000001f) return false;
    point = {point.x - m[4], point.y - m[5]};
    out = {(m[1] * point.x - m[2] * point.y) / det, (m[0] * point.y - m[3] * point.x) / det};
    return std::isfinite(out.x) && std::isfinite(out.y);
}
uintptr_t transform_slot(uintptr_t clip) {
    if (!clip) return 0;
    const auto slot = *reinterpret_cast<int32_t*>(clip + 0xc);
    const auto context = *reinterpret_cast<uintptr_t*>(clip + 0x10);
    if (slot < 0 || !context) return 0;
    const auto transforms = *reinterpret_cast<uintptr_t*>(context + 0x80);
    return transforms ? transforms + static_cast<uintptr_t>(slot) * 0x40 : 0;
}
bool affine_to(uintptr_t clip, uintptr_t ancestor, uintptr_t movie, Point point, Point& out) {
    for (unsigned i = 0; i < 16 && clip != ancestor; ++i) {
        if (!clip || *reinterpret_cast<uintptr_t*>(clip + 0x30) != movie) return false;
        const auto t = transform_slot(clip);
        if (!t) return false;
        point = {*reinterpret_cast<float*>(t + 0x4) * point.x + *reinterpret_cast<float*>(t + 0xc) * point.y +
                     *reinterpret_cast<float*>(t + 0x14), *reinterpret_cast<float*>(t + 0x10) * point.x +
                     *reinterpret_cast<float*>(t + 0x8) * point.y + *reinterpret_cast<float*>(t + 0x18)};
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        clip = *reinterpret_cast<uintptr_t*>(clip + 0x40);
    }
    out = point;
    return clip == ancestor;
}
bool bounds(uintptr_t clip, uintptr_t parent, Rect& out, uintptr_t placement = 0) {
    // convert the cached render rectangle directly into weaponinfo space
    Rect local{};
    if (!raw_bounds(clip, local)) return false;
    const auto movie = *reinterpret_cast<uintptr_t*>(clip + 0x30);
    const Point corners[4]{{local.tl.x, local.tl.y}, {local.br.x, local.tl.y},
                           {local.tl.x, local.br.y}, {local.br.x, local.br.y}};
    Point point{}, local_point{};
    if (!unrender(clip, corners[0], local_point) || !affine_to(placement ? placement : clip, parent, movie, local_point, point)) return false;
    out = {point, point};
    for (unsigned i = 1; i < 4; ++i) {
        if (!unrender(clip, corners[i], local_point) || !affine_to(placement ? placement : clip, parent, movie, local_point, point)) return false;
        out.tl.x = std::min(out.tl.x, point.x);
        out.tl.y = std::min(out.tl.y, point.y);
        out.br.x = std::max(out.br.x, point.x);
        out.br.y = std::max(out.br.y, point.y);
    }
    return out.br.x > out.tl.x && out.br.y > out.tl.y;
}
bool from_space(uintptr_t clip, uintptr_t parent, uintptr_t movie, Point point, Point& out, bool delta = false) {
    Point origin{}, x{}, y{};
    if (!affine_to(clip, parent, movie, {0, 0}, origin) || !affine_to(clip, parent, movie, {1, 0}, x) ||
        !affine_to(clip, parent, movie, {0, 1}, y)) return false;
    x = {x.x - origin.x, x.y - origin.y};
    y = {y.x - origin.x, y.y - origin.y};
    const float det = x.x * y.y - y.x * x.y;
    const float area = std::fabs(x.x * y.y) + std::fabs(y.x * x.y);
    if (!std::isfinite(det) || std::fabs(det) <= area * 0.000001f) return false;
    if (!delta) point = {point.x - origin.x, point.y - origin.y};
    out = {(y.y * point.x - y.x * point.y) / det, (x.x * point.y - x.y * point.x) / det};
    return std::isfinite(out.x) && std::isfinite(out.y);
}
bool match_linear(uintptr_t clone, uintptr_t source, uintptr_t parent, uintptr_t movie, float scale = 1.0f) {
    Point origin{}, x{}, y{};
    if (!affine_to(source, parent, movie, {0, 0}, origin) || !affine_to(source, parent, movie, {1, 0}, x) ||
        !affine_to(source, parent, movie, {0, 1}, y)) return false;
    const auto t = transform_slot(clone);
    if (!t) return false;
    const float values[4]{(x.x - origin.x) * scale, (y.y - origin.y) * scale, (y.x - origin.x) * scale, (x.y - origin.y) * scale};
    const unsigned offsets[4]{0x4, 0x8, 0xc, 0x10};
    bool changed = false;
    for (unsigned i = 0; i < 4; ++i) {
        auto& current = *reinterpret_cast<float*>(t + offsets[i]);
        if (current != values[i]) { current = values[i]; changed = true; }
    }
    if (changed) swf.dirty(clone);
    return true;
}
bool visual_offset(uintptr_t source, uintptr_t visual_leaf, uintptr_t parent, uintptr_t movie, Point& out) {
    Rect rect{};
    Point origin{};
    if (!bounds(visual_leaf, parent, rect) || !affine_to(source, parent, movie, {0, 0}, origin)) return false;
    const Point visual = center(rect);
    out = {visual.x - origin.x, visual.y - origin.y};
    return true;
}
bool place_visual(uintptr_t clone, uintptr_t source, uintptr_t parent, uintptr_t movie, Point desired, Point offset, float scale = 1.0f) {
    if (!match_linear(clone, source, parent, movie, scale)) return false;
    place(clone, {desired.x - offset.x * scale, desired.y - offset.y * scale});
    return true;
}

struct NativePosition {
    uintptr_t root = 0, parent = 0, movie = 0;
    uint64_t epoch = 0;
    Point base{}, applied{};
};

bool remember_native(NativePosition& saved, uintptr_t root, uintptr_t movie, uint64_t epoch) {
    Point current{};
    if (!root || *reinterpret_cast<uintptr_t*>(root + 0x30) != movie || !position(root, current)) return false;
    const auto parent = *reinterpret_cast<uintptr_t*>(root + 0x40);
    if (saved.root != root || saved.parent != parent || saved.movie != movie ||
        saved.epoch != epoch || current.x != saved.applied.x || current.y != saved.applied.y) {
        saved = {root, parent, movie, epoch, current, current};
    }
    return true;
}
bool native_visual_center(const NativePosition& saved, uintptr_t leaf, uintptr_t parent, Point& out) {
    Point origin{}, offset{};
    if (!affine_to(saved.parent, parent, saved.movie, saved.base, origin) || !visual_offset(saved.root, leaf, parent, saved.movie, offset)) return false;
    out = {origin.x + offset.x, origin.y + offset.y};
    return std::isfinite(out.x) && std::isfinite(out.y);
}
bool move_native(NativePosition& saved, uintptr_t parent, Point parent_delta) {
    Point local{};
    if (!from_space(saved.parent, parent, saved.movie, parent_delta, local, true)) return false;
    saved.applied = {saved.base.x + local.x, saved.base.y + local.y};
    place(saved.root, saved.applied);
    return true;
}
void restore_native(NativePosition& saved, uintptr_t root, uintptr_t movie, uint64_t epoch) {
    if (root && saved.root == root && saved.movie == movie && saved.epoch == epoch) {
        place(root, saved.base);
        saved.applied = saved.base;
    }
}
bool key_name(unsigned vk, char (&name)[16]) {
    if (!vk) return false;
    if (vk >= VK_F1 && vk <= VK_F12) {
        std::snprintf(name, sizeof(name), "F%u", vk - VK_F1 + 1);
        return true;
    }
    const auto scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    return scan && GetKeyNameTextA(static_cast<LONG>(scan << 16), name, sizeof(name)) > 0;
}

// clone native sprites under their live swf parent, without copying objects
uintptr_t clone(uintptr_t source, uintptr_t parent, const char* name, bool& created) {
    if (const auto found = child(parent, name)) return found;
    if (!source || !parent) return 0;
    const auto source_parent = *reinterpret_cast<uintptr_t*>(source + 0x40);
    if (!source_parent || *reinterpret_cast<uintptr_t*>(source + 0x30) != *reinterpret_cast<uintptr_t*>(parent + 0x30)) return 0;
    String native_name{};
    swf.string_init(&native_name);
    uintptr_t result = 0;
    __try {
        swf.string_set(&native_name, name);
        if (source_parent == parent) result = swf.duplicate(source, &native_name);
        else {
            const auto entry = swf.entry(source_parent, *reinterpret_cast<int32_t*>(source + 0x48));
            if (entry && *reinterpret_cast<uintptr_t*>(entry + 0x30) == source) {
                const auto depth = *reinterpret_cast<int32_t*>(parent + 0x78) + 0x4001;
                const auto added = swf.add(parent, depth, *reinterpret_cast<uint16_t*>(entry), &native_name);
                result = added ? *reinterpret_cast<uintptr_t*>(added + 0x30) : 0;
                if (result) { swf.dirty(result); swf.start(result, 1); }
            }
        }
    } __finally { swf.string_free(&native_name); }
    if (result) show(result, false);
    created |= result != 0;
    return result;
}

struct GraphicsSource {
    uintptr_t widget = 0, hammer = 0, quickuse = 0, flame = 0;
    uintptr_t primary_root = 0, flame_root = 0, crucible_root = 0, hammer_root = 0;
    uintptr_t parent = 0, movie = 0, source = 0;
    bool crucible_movie_match = false, hammer_movie_match = false;
};

bool matches_movie(uintptr_t root, uintptr_t movie) {
    if (!root || *reinterpret_cast<uintptr_t*>(root + 0x30) != movie) return false;
    const auto parent = *reinterpret_cast<uintptr_t*>(root + 0x40);
    return parent && *reinterpret_cast<uintptr_t*>(parent + 0x30) == movie;
}
uintptr_t clip_root(uintptr_t widget) { return widget ? *reinterpret_cast<uintptr_t*>(widget + 0x18) : 0; }

GraphicsSource graphics_source(uintptr_t element) {
    GraphicsSource s{};
    s.widget = *reinterpret_cast<uintptr_t*>(element + 0x1e8);
    s.hammer = *reinterpret_cast<uintptr_t*>(element + 0x1f8);
    s.quickuse = *reinterpret_cast<uintptr_t*>(element + 0x1d0);
    s.flame = *reinterpret_cast<uintptr_t*>(element + 0x1d8);
    s.primary_root = clip_root(s.quickuse);
    s.flame_root = clip_root(s.flame);
    s.crucible_root = clip_root(s.widget);
    s.hammer_root = clip_root(s.hammer);
    s.parent = s.primary_root ? *reinterpret_cast<uintptr_t*>(s.primary_root + 0x40) : 0;
    s.movie = s.parent ? *reinterpret_cast<uintptr_t*>(s.parent + 0x30) : 0;
    if (!s.movie || !s.flame_root || *reinterpret_cast<uintptr_t*>(s.primary_root + 0x30) != s.movie ||
        *reinterpret_cast<uintptr_t*>(s.flame_root + 0x30) != s.movie || *reinterpret_cast<uintptr_t*>(s.flame_root + 0x40) != s.parent) return s;
    s.crucible_movie_match = matches_movie(s.crucible_root, s.movie);
    s.hammer_movie_match = matches_movie(s.hammer_root, s.movie);
    if (s.crucible_movie_match && child(s.crucible_root, "icon") && child(s.crucible_root, "pips")) s.source = s.crucible_root;
    else if (s.hammer_movie_match && child(s.hammer_root, "icon") && child(s.hammer_root, "pips")) s.source = s.hammer_root;
    return s;
}

struct ProjectionFrame {
    uintptr_t element;
    const HudOwnerSnapshot& owner;
    const GraphicsSource& context;
    unsigned keys;
    uint64_t epoch;
    bool& clips_applied;
    NativePosition& crucible_position;
    NativePosition& hammer_position;
    std::mutex& state_mutex;
    struct MaterialAttempt {
        uintptr_t owner, parent, clip, icon, material;
        uint64_t epoch, retry_at;
        bool applied;
    };
    bool active_cached{}, active_icon_visual{}, active_movie{}, active_position_valid{}, active_stage_forward{};
    bool arrow_origins{}, arrow_ready{}, arrow_visual{}, created{}, flame_visual{}, graphics_applied{}, donor_visual{};
    bool known{}, native_arrow_visual{}, native_backer_visual{}, native_layout{}, new_refill{}, refill_frame_changed{};
    bool refill_offsets{}, refill_visual{}, source_cached{}, source_icon_visual{}, switch_available{}, visible_plate{};
    uintptr_t active_icon{}, active_root{}, ammo_donor{}, ammo_glyph{}, arrow_geometry{}, arrow_source{};
    uintptr_t crucible_root{}, donor{}, flame_root{}, hammer{}, inactive_root{}, layout_source{}, movie{};
    uintptr_t native_ammo{}, native_arrow{}, native_backer{}, parent{}, primary_root{}, quickuse{}, refill{};
    uintptr_t refill_bind{}, refill_geometry{}, refill_plate{}, source{}, source_icon{}, special_arrow{};
    uintptr_t special_bind{}, three{}, widget{};
    NativePosition *active_position{}, *inactive_position{};
    Point adjacent{}, ammo_x{}, ammo_y{}, anchor{}, arrow_offset{}, authored_arrow_origin{}, refill_anchor{};
    Point refill_key_target{}, refill_offset{}, refill_plate_offset{}, refill_target{}, refill_visual_target{};
    Point special_target{}, toggle_key_target{}, toggle_target{};
    int32_t ammo_icon_path_hash{};
    Rect arrow_bounds{}, backer_bounds{}, donor_bounds{}, flame_icon_bounds{}, glyph_bounds{};
    Rect intended_refill_bounds{}, refill_plate_bounds{}, source_icon_bounds{};
    MaterialAttempt attempt{};
    const char *bind_failure{}, *switch_failure{};
    float glyph_scale{}, icon_baseline_y{}, keycap_baseline_y{}, native_gap{}, render_row_slope{}, special_x{};
    float switch_width{};
    const float* render_matrix{};

    bool fail_refill(const char* reason, uintptr_t detail) {
        show(refill, false);
        if (ammo_glyph) show(ammo_glyph, false);
        if (refill_bind) show(refill_bind, false);
        hud_trace.record(save::BStage::profile_prepare, save::BStatus::refused, reason, epoch,
                         {{"detail", detail},
                          {"source_icon_tl", trace_point(source_icon_bounds.tl)},
                          {"source_icon_br", trace_point(source_icon_bounds.br)},
                          {"flame_icon_tl", trace_point(flame_icon_bounds.tl)},
                          {"flame_icon_br", trace_point(flame_icon_bounds.br)},
                          {"native_arrow_tl", trace_point(arrow_bounds.tl)},
                          {"native_arrow_br", trace_point(arrow_bounds.br)},
                          {"refill_icon_tl", trace_point(intended_refill_bounds.tl)},
                          {"refill_icon_br", trace_point(intended_refill_bounds.br)},
                          {"gap", trace_point({native_gap, 0})},
                          {"icon_baseline_y", trace_point({0, icon_baseline_y})},
                          {"refill_center", trace_point(refill_target)}}, element);
        refuse(reason, element, detail, epoch);
        return false;
    }
    bool apply_keycap_text(uintptr_t clip, uintptr_t value, const char* label, const char* missing_text) {
        if (!text_is(value, label)) swf.set_text(value, label);
        if (!text_is(value, label)) {
            show(clip, false);
            bind_failure = missing_text;
            return false;
        }
        return true;
    }
    bool unavailable_key(uintptr_t clip, unsigned vk, const char* missing_text) {
        if (clip) show(clip, false);
        if (vk) bind_failure = missing_text;
        return vk == 0;
    }
    bool bind_key(uintptr_t& clip, const char* name, unsigned vk, Point visual_at, const char* missing_clip, const char* missing_text) {
        char label[16]{};
        if (!key_name(vk, label)) return unavailable_key(clip, vk, missing_text);
        if (!clip) clip = clone(donor, parent, name, created);
        if (!clip) {
            bind_failure = missing_clip;
            return false;
        }
        hold_frame(clip, 1);
        const auto kbm = child(clip, "kbm");
        const auto joy = child(clip, "joy");
        const auto value = text_child(kbm, "txtVal");
        if (!kbm || !joy || !value) {
            show(clip, false);
            bind_failure = value ? missing_clip : missing_text;
            return false;
        }
        hold_frame(kbm, 1);
        hold_frame(joy, 1);
        if (!apply_keycap_text(clip, value, label, missing_text)) return false;
        show(joy, false);
        show(kbm, true);
        if (!match_linear(clip, donor, parent, movie) || !match_linear(kbm, child(donor, "kbm"), donor, movie)) {
            show(clip, false);
            bind_failure = "hud_keycap_transform_failed";
            return false;
        }
        Point visible_offset{};
        Rect keycap_bounds{};
        if (!bounds(kbm, parent, keycap_bounds) || !visual_offset(clip, kbm, parent, movie, visible_offset)) {
            place(clip, visual_at);
            show(clip, true);
            swf.dirty(clip);
            bind_failure = "hud_keycap_geometry_pending";
            return false;
        }
        const Point keycap_center = visual_at;
        if (!place_visual(clip, clip, parent, movie, keycap_center, visible_offset)) {
            show(clip, false);
            bind_failure = "hud_keycap_visual_bounds_missing";
            return false;
        }
        show(clip, true);
        return true;
    }
    void hide_unavailable_clips() {
        if (parent && movie && primary_root && flame_root && *reinterpret_cast<uintptr_t*>(parent + 0x30) == movie &&
            *reinterpret_cast<uintptr_t*>(primary_root + 0x30) == movie && *reinterpret_cast<uintptr_t*>(primary_root + 0x40) == parent &&
            *reinterpret_cast<uintptr_t*>(flame_root + 0x30) == movie && *reinterpret_cast<uintptr_t*>(flame_root + 0x40) == parent) {
            for (const auto name : {"apAmmoRefill", "apAmmoGlyph", "apSpecialSwitch", "apAmmoRefillBind", "apSpecialToggleBind"})
                if (const auto clip = child(parent, name)) show(clip, false);
        }
    }
    bool source_available() {
        struct Refusal {
            uintptr_t element, widget, hammer, primary, flame, parent, movie;
            uint64_t epoch;
        };
        static Refusal last{};
        if (!source) {
            restore_native(crucible_position, context.crucible_root, movie, epoch);
            restore_native(hammer_position, context.hammer_root, movie, epoch);
            hide_unavailable_clips();
            const Refusal current{element, widget, hammer, primary_root, flame_root, parent, movie, epoch};
            {
                std::lock_guard lock(state_mutex);
                if (last.element == current.element && last.widget == current.widget && last.hammer == current.hammer && last.primary == current.primary &&
                    last.flame == current.flame && last.parent == current.parent && last.movie == current.movie && last.epoch == current.epoch)
                    return false;
                last = current;
            }
            hud_trace.record(save::BStage::profile_output, save::BStatus::refused, "hud_graphics_source_unavailable", 0,
                             {{"owner", element},
                              {"widget", widget},
                              {"hammer", hammer},
                              {"quickuse", quickuse},
                              {"flame", context.flame},
                              {"primary_root", primary_root},
                              {"flame_root", flame_root},
                              {"parent", parent},
                              {"movie", movie},
                              {"crucible_root", crucible_root},
                              {"crucible_movie_match", context.crucible_movie_match},
                              {"hammer_root", context.hammer_root},
                              {"hammer_movie_match", context.hammer_movie_match},
                              {"generation", epoch}});
            return false;
        }
        {
            std::lock_guard lock(state_mutex);
            last = {};
        }
        return true;
    }
    void invalidate() {
        restore_native(crucible_position, context.crucible_root, movie, epoch);
        restore_native(hammer_position, context.hammer_root, movie, epoch);
        if (refill) show(refill, false);
        if (ammo_glyph) show(ammo_glyph, false);
        if (special_arrow) show(special_arrow, false);
        if (refill_bind) show(refill_bind, false);
        if (special_bind) show(special_bind, false);
        hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded, "hud_invalidated", 0,
                         {{"owner", element},
                          {"parent", parent},
                          {"generation", epoch},
                          {"snapshot_valid", owner.namespace_valid},
                          {"context_valid", 1}});
    }
    bool prepare() {
        if (!graphics_ready) {
            restore_native(crucible_position, context.crucible_root, context.movie, epoch);
            restore_native(hammer_position, context.hammer_root, context.movie, epoch);
            refuse("hud_graphics_unavailable", element, 0, epoch);
            return false;
        }
        source = context.source;
        // the refill template supplies pips; geometry comes from a rendered special
        layout_source = source;
        parent = context.parent;
        movie = context.movie;
        Rect source_probe{};
        if (source && !bounds(child(source, "icon"), parent, source_probe)) {
            const auto selected_root = owner.selected == SC_SPECIAL_WEAPON_HAMMER ? context.hammer_root : context.crucible_root;
            if (selected_root && bounds(child(selected_root, "icon"), parent, source_probe) && child(selected_root, "icon")) layout_source = selected_root;
        }
        primary_root = context.primary_root;
        flame_root = context.flame_root;
        crucible_root = context.crucible_root;
        quickuse = context.quickuse;
        widget = context.widget;
        hammer = context.hammer;
        if (!source_available()) return false;
        anchor = {};
        adjacent = {};
        if (!position(primary_root, anchor) || !position(flame_root, adjacent)) {
            refuse("hud_swf_context_unavailable", element, source, epoch);
            return false;
        }
        refill = child(parent, "apAmmoRefill");
        ammo_glyph = child(parent, "apAmmoGlyph");
        special_arrow = child(parent, "apSpecialSwitch");
        refill_bind = child(parent, "apAmmoRefillBind");
        special_bind = child(parent, "apSpecialToggleBind");
        const bool visible = owner.namespace_valid && *reinterpret_cast<uint8_t*>(element + 0x209);
        // all references come from this update's live parent, including invalidation
        if (!visible) {
            invalidate();
            return false;
        }
        return true;
    }
    void create_switch_clip() {
        if (!switch_available || !arrow_source) return;
        if (!special_arrow) special_arrow = clone(arrow_source, parent, "apSpecialSwitch", created);
        if (!special_arrow) return;
        hold_frame(special_arrow, 1);
        if (const auto leaf = child(special_arrow, "arrow")) show(leaf, true);
        if (const auto leaf = child(special_arrow, "backer")) show(leaf, true);
        if (const auto cta = child(special_arrow, "cta")) show(cta, false);
        if (const auto cta = child(child(special_arrow, "icon"), "cta")) show(cta, false);
        show(special_arrow, true);
        if (created) swf.dirty(special_arrow);
        arrow_geometry = special_arrow;
        native_arrow = child(special_arrow, "arrow");
        native_backer = child(special_arrow, "backer");
    }
    bool create_clips() {
        created = false;
        new_refill = !refill;
        if (!refill) refill = clone(source, parent, "apAmmoRefill", created);
        if (!refill) {
            refuse("hud_refill_clone_failed", element, source, epoch);
            return false;
        }
        refill_frame_changed = hold_frame(refill, owner.refill_enabled ? 1 : 2);
        show(refill, true);
        if (new_refill || refill_frame_changed) swf.dirty(refill);
        switch_available = owner.owns_crucible && owner.owns_hammer && route_ready.load(std::memory_order_acquire) && ((keys >> 8) & 0xff);
        arrow_source = child(parent, "swapEquipment");
        arrow_geometry = arrow_source;
        native_arrow = child(arrow_source, "arrow");
        native_backer = child(arrow_source, "backer");
        create_switch_clip();
        return true;
    }
    void select_special_roots() {
        active_root = owner.selected == SC_SPECIAL_WEAPON_CRUCIBLE ? context.crucible_root
                      : owner.selected == SC_SPECIAL_WEAPON_HAMMER ? context.hammer_root : uintptr_t{0};
        active_position = &(owner.selected == SC_SPECIAL_WEAPON_HAMMER ? hammer_position : crucible_position);
        inactive_position = &(owner.selected == SC_SPECIAL_WEAPON_HAMMER ? crucible_position : hammer_position);
        inactive_root = owner.selected == SC_SPECIAL_WEAPON_HAMMER ? context.crucible_root : context.hammer_root;
        arrow_bounds = {};
        backer_bounds = {};
        source_icon_bounds = {};
        flame_icon_bounds = {};
    }
    bool measure_flame_plate(uintptr_t flame_plate, uintptr_t flame_icon) {
        return bounds(child(source, "background"), parent, flame_icon_bounds, flame_plate ? flame_plate : flame_root) ||
               bounds(flame_plate, parent, flame_icon_bounds) || bounds(flame_icon, parent, flame_icon_bounds) ||
               bounds(flame_root, parent, flame_icon_bounds) || bounds(child(primary_root, "background"), parent, flame_icon_bounds,
                      flame_plate ? flame_plate : flame_root) || bounds(child(active_root, "background"), parent, flame_icon_bounds,
                      flame_plate ? flame_plate : flame_root);
    }
    void read_special_geometry() {
        select_special_roots();
        Rect active_icon_bounds{}, active_glyph_bounds{};
        Rect clone_plate_bounds{};
        refill_geometry = !refill_frame_changed && bounds(child(refill, "background"), parent, clone_plate_bounds) ? refill : source;
        source_icon = child(layout_source, "icon");
        const auto flame_icon = child(flame_root, "icon");
        active_icon = child(active_root, "icon");
        native_arrow_visual = bounds(native_arrow, parent, arrow_bounds);
        native_backer_visual = bounds(native_backer, parent, backer_bounds);
        source_icon_visual = bounds(source_icon, parent, source_icon_bounds);
        const auto flame_plate = child(flame_root, "background");
        flame_visual = measure_flame_plate(flame_plate, flame_icon);
        active_icon_visual = bounds(child(active_root, "background"), parent, active_icon_bounds) && bounds(active_icon, parent, active_glyph_bounds);
        active_movie = active_root && *reinterpret_cast<uintptr_t*>(active_root + 0x30) == movie;
        Point active_now{};
        active_position_valid = active_movie && position(active_root, active_now);
        active_cached = active_position_valid && remember_native((*active_position), active_root, movie, epoch);
        Point original_special{};
        active_stage_forward = active_cached && active_icon_visual && native_visual_center((*active_position), active_icon, parent, original_special);
        auto& source_position = layout_source == context.hammer_root ? hammer_position : crucible_position;
        source_cached = remember_native(source_position, layout_source, movie, epoch);
    }
    void measure_layout() {
        native_gap = 1.0f;
        constexpr float switch_gap = -40.0f;
        const Rect switch_bounds = native_backer_visual ? Rect{{std::min(arrow_bounds.tl.x, backer_bounds.tl.x),
                                               std::min(arrow_bounds.tl.y, backer_bounds.tl.y)},
                                              {std::max(arrow_bounds.br.x, backer_bounds.br.x), std::max(arrow_bounds.br.y, backer_bounds.br.y)}}
                                       : arrow_bounds;
        icon_baseline_y = adjacent.y;
        render_matrix = reinterpret_cast<const float*>(parent + 0x90);
        const float render_yy = render_matrix[1];
        const float render_yx = render_matrix[3];
        render_row_slope = std::isfinite(render_yy) && std::isfinite(render_yx) && std::fabs(render_yy) > 0.000001f ? render_yx / render_yy : 0.0f;
        authored_arrow_origin = {};
        arrow_origins = position(arrow_source, authored_arrow_origin);
        const Point arrow_baseline = center(flame_icon_bounds);
        switch_width = switch_bounds.br.x - switch_bounds.tl.x;
        const float toggle_x = flame_icon_bounds.tl.x - switch_gap - switch_width * 0.5f + center(arrow_bounds).x - center(switch_bounds).x;
        toggle_target = {toggle_x, arrow_baseline.y + center(arrow_bounds).y - center(switch_bounds).y - (toggle_x - arrow_baseline.x) * render_row_slope};
        special_x = flame_icon_bounds.tl.x - switch_gap * 2 - switch_width - (flame_icon_bounds.br.x - flame_icon_bounds.tl.x) * 0.5f;
        const Point flame_center = center(flame_icon_bounds);
        special_target = {special_x, flame_center.y - (special_x - flame_center.x) * render_row_slope};
    }
    bool native_layout_ready() {
        return native_arrow_visual && flame_visual && active_stage_forward && source_cached &&
               source_icon_visual && std::isfinite(special_x) && switch_width > 0;
    }
    void measure_refill() {
        refill_plate = child(refill_geometry, "background");
        refill_plate_bounds = {};
        refill_plate_offset = {};
        visible_plate = bounds(refill_plate, parent, refill_plate_bounds) && visual_offset(refill_geometry, refill_plate, parent, movie, refill_plate_offset);
        if (!visible_plate && source_icon_visual) refill_plate_bounds = source_icon_bounds;
        // reserve the equipment columns even while their native sprites are hidden
        const float row_right = std::max({anchor.x, adjacent.x, authored_arrow_origin.x}) + (refill_plate_bounds.br.x - refill_plate_bounds.tl.x) * 0.5f;
        const Point refill_icon_target{row_right + native_gap + (refill_plate_bounds.br.x - refill_plate_bounds.tl.x) * 0.5f, icon_baseline_y};
        refill_offset = {};
        arrow_offset = {};
        refill_target = {};
        Point refill_lift{};
        const auto geometry_anchor = refill_plate ? refill_plate : child(refill_geometry, "icon");
        refill_offsets = visual_offset(refill_geometry, geometry_anchor, parent, movie, refill_offset) &&
                         from_space(parent, *reinterpret_cast<uintptr_t*>(parent + 0x40), movie, {0, -4.0f}, refill_lift, true);
        if (refill_offsets) refill_target = refill_icon_target;
        const float refill_x = refill_target.x + refill_lift.x;
        const Point refill_baseline{adjacent.x + refill_offset.x, adjacent.y + refill_offset.y};
        refill_visual_target = {refill_x, refill_baseline.y + (refill_x - refill_baseline.x) * render_row_slope};
        native_layout = native_layout_ready();
        refill_visual = refill_offsets && (visible_plate || source_icon_visual) && std::isfinite(native_gap) && native_gap > 0;
        arrow_visual = native_layout && visual_offset(arrow_geometry, native_arrow, parent, movie, arrow_offset);
    }
    const char* active_geometry_failure() {
        if (!active_root) return "switch_active_special_root_missing";
        if (!active_movie) return "switch_active_special_movie_mismatch";
        if (!active_position_valid) return "switch_active_special_position_missing";
        if (!active_cached) return "switch_active_bounds_cache_failed";
        if (!active_icon_visual) return "switch_active_icon_bounds_missing";
        if (!active_stage_forward) return "switch_active_stage_forward_failed";
        if (!source_icon_visual) return "switch_source_icon_bounds_missing";
        if (!source_cached) return "switch_source_bounds_cache_failed";
        if (!native_layout) return "switch_native_gap_invalid";
        if (!arrow_visual) return "switch_arrow_stage_forward_failed";
        if (!arrow_origins) return "switch_authored_origin_missing";
        return nullptr;
    }
    void find_switch_failure() {
        switch_failure = nullptr;
        if (switch_available) {
            if (!arrow_source) switch_failure = "switch_source_missing";
            else if (!native_arrow) switch_failure = "switch_arrow_leaf_missing";
            else if (!native_arrow_visual) {
                Rect raw{};
                switch_failure = raw_bounds(native_arrow, raw) ? "switch_arrow_stage_transform_failed" : "switch_arrow_bounds_missing";
            } else switch_failure = active_geometry_failure();
        }
    }
    void move_special_position(NativePosition* saved) {
        const auto root = saved == &(*active_position) ? active_root : inactive_root;
        Point original{}, reference{};
        const auto vertical_leaf = child(root, "background");
        Rect reference_bounds{};
        // native glyphs animate inside fixed equipment plates
        if (!remember_native(*saved, root, movie, epoch) || !native_visual_center(*saved, vertical_leaf, parent, original)) {
            if (saved == &(*active_position)) switch_failure = "switch_move_active_failed";
            return;
        }
        if (!bounds(child(flame_root, "background"), parent, reference_bounds)) reference_bounds = flame_icon_bounds;
        reference = center(reference_bounds);
        const float target_y = reference.y - (special_target.x - reference.x) * render_row_slope;
        if (!move_native(*saved, parent, {special_target.x - original.x, target_y - original.y})) {
            if (saved == &(*active_position)) switch_failure = "switch_move_active_failed";
        }
    }
    void move_specials() {
        for (auto* saved : {&(*active_position), &(*inactive_position)}) {
            move_special_position(saved);
        }
    }
    void place_arrow() {
        if (!switch_available || switch_failure) return;
        if (!special_arrow) special_arrow = clone(arrow_source, parent, "apSpecialSwitch", created);
        if (!special_arrow) {
            switch_failure = "switch_clone_failed";
            return;
        }
        hold_frame(special_arrow, 1);
        if (const auto backer = child(special_arrow, "backer")) show(backer, true);
        if (const auto cta = child(special_arrow, "cta")) show(cta, false);
        if (const auto cta = child(child(special_arrow, "icon"), "cta")) show(cta, false);
        if (const auto leaf = child(special_arrow, "arrow")) show(leaf, true);
        else switch_failure = "switch_arrow_leaf_missing";
        if (!switch_failure && !match_linear(special_arrow, arrow_source, parent, movie)) switch_failure = "switch_arrow_place_failed";
        if (!switch_failure && !place_visual(special_arrow, arrow_source, parent, movie, toggle_target, arrow_offset))
            switch_failure = "switch_arrow_place_failed";
        move_specials();
        if (!switch_failure) {
            show(special_arrow, true);
            arrow_ready = true;
        }
    }
    void place_switch() {
        arrow_ready = false;
        place_arrow();
        if (!arrow_ready) {
            restore_native(crucible_position, context.crucible_root, movie, epoch);
            restore_native(hammer_position, context.hammer_root, movie, epoch);
            if (special_arrow && !(switch_available && arrow_geometry == special_arrow && !native_arrow_visual)) show(special_arrow, false);
        }
    }
    void trace_layout() {
        Rect native_special_bounds{}, ap_arrow_bounds{};
        bounds(active_icon, parent, native_special_bounds);
        bounds(native_arrow, parent, ap_arrow_bounds);
        const float refill_half_width = (refill_plate_bounds.br.x - refill_plate_bounds.tl.x) * 0.5f;
        const float refill_half_height = (refill_plate_bounds.br.y - refill_plate_bounds.tl.y) * 0.5f;
        intended_refill_bounds = {
            {refill_visual_target.x - refill_half_width, refill_visual_target.y - refill_half_height},
            {refill_visual_target.x + refill_half_width, refill_visual_target.y + refill_half_height}};
        hud_trace.record(save::BStage::profile_prepare, switch_failure || !refill_visual ? save::BStatus::refused : save::BStatus::succeeded,
                         switch_failure  ? switch_failure
                         : refill_visual ? "hud_visual_layout_ready" : "hud_visual_layout_unavailable", epoch,
                         {{"parent", parent},
                          {"selected", owner.selected},
                          {"special_icon_tl", trace_point(native_special_bounds.tl)},
                          {"special_icon_br", trace_point(native_special_bounds.br)},
                          {"flame_icon_tl", trace_point(flame_icon_bounds.tl)},
                          {"flame_icon_br", trace_point(flame_icon_bounds.br)},
                          {"native_arrow_tl", trace_point(arrow_bounds.tl)},
                          {"native_arrow_br", trace_point(arrow_bounds.br)},
                          {"ap_arrow_tl", trace_point(ap_arrow_bounds.tl)},
                          {"ap_arrow_br", trace_point(ap_arrow_bounds.br)},
                          {"refill_icon_tl", trace_point(intended_refill_bounds.tl)},
                          {"refill_icon_br", trace_point(intended_refill_bounds.br)},
                          {"gap", trace_point({native_gap, 0})},
                          {"icon_baseline_y", trace_point({0, icon_baseline_y})},
                          {"f9_center", trace_point(refill_target)},
                          {"f10_center", trace_point(toggle_target)}}, element);
    }
    bool prime_refill_transform() {
        if (!match_linear(refill, source, parent, movie)) {
            show(refill, false);
            refuse("hud_refill_transform_failed", element, source, epoch);
            return false;
        }
        show(refill, true);
        swf.dirty(refill);
        if (ammo_glyph) show(ammo_glyph, false);
        if (refill_bind) show(refill_bind, false);
        if (special_bind) show(special_bind, false);
        refuse("hud_refill_geometry_pending", element, refill, epoch);
        return false;
    }
    bool refill_geometry_ready() {
        if (!refill_visual) {
            if (!refill_offsets && child(refill, "background")) return prime_refill_transform();
            show(refill, !refill_geometry && source != layout_source);
            if (ammo_glyph) show(ammo_glyph, false);
            if (refill_bind) show(refill_bind, false);
            if (special_bind) show(special_bind, false);
            refuse(switch_failure        ? switch_failure : !source_icon_visual ? "hud_refill_source_icon_bounds_missing"
                                         : "hud_refill_stage_transform_failed", element, source, epoch);
            return false;
        }
        return true;
    }
    bool prepare_ammo() {
        known = owner.refill_balance <= 3;
        // the equipment clone carries its donor cta; the ap labels are separate clips
        if (const auto cta = child(child(refill, "icon"), "cta")) show(cta, false);
        const auto icon_group = child(refill, "icon");
        if (const auto small = child(icon_group, "iconSmall")) show(small, false);
        if (const auto static_icon = child(icon_group, "iconStatic")) show(static_icon, false);
        const auto equipped_weapon = child(parent, "equippedWeapon");
        native_ammo = child(child(equipped_weapon, "ammoIcon"), "image");
        ammo_donor = ammo_glyph ? ammo_glyph : native_ammo;
        if (!ammo_donor) return fail_refill("hud_ammo_leaf_missing", equipped_weapon);
        if (*reinterpret_cast<uintptr_t*>(ammo_donor + 0x30) != movie) return fail_refill("hud_ammo_leaf_movie_mismatch", native_ammo);
        ammo_icon_path_hash = *reinterpret_cast<int32_t*>(ammo_donor + 0x20);
        if (!ammo_glyph) ammo_glyph = clone(ammo_donor, parent, "apAmmoGlyph", created);
        if (!ammo_glyph) return fail_refill("hud_ammo_leaf_clone_failed", native_ammo);
        return true;
    }
    bool apply_ammo_material() {
        static MaterialAttempt shared_attempt{};
        attempt = {};
        {
            std::lock_guard lock(state_mutex);
            attempt = shared_attempt;
        }
        if (new_refill || attempt.owner != element || attempt.parent != parent || attempt.clip != refill ||
            attempt.icon != ammo_glyph || attempt.epoch != epoch)
            attempt = {element, parent, refill, ammo_glyph, 0, epoch, 0, false};
        if (attempt.applied && *reinterpret_cast<uintptr_t*>(ammo_glyph + 0x60) != attempt.material) {
            attempt.applied = false;
            attempt.retry_at = 0;
        }
        if (!attempt.applied && GetTickCount64() >= attempt.retry_at) {
            attempt.retry_at = GetTickCount64() + 1000;
            const auto material = swf.find_material(image_base + 0x5e05200, "art/ui/icons/ammo/bullets", 0);
            if (material) {
                swf.material(ammo_glyph, material, -1, -1, 0);
                attempt.material = material;
                attempt.applied = *reinterpret_cast<uintptr_t*>(ammo_glyph + 0x60) == material && *reinterpret_cast<uint16_t*>(ammo_glyph + 0x68) != 0 &&
                                  *reinterpret_cast<uint16_t*>(ammo_glyph + 0x6a) != 0;
            }
        }
        {
            std::lock_guard lock(state_mutex);
            shared_attempt = attempt;
        }
        if (!attempt.applied) { return fail_refill("hud_ammo_material_unavailable", ammo_glyph); }
        return true;
    }
    bool measure_ammo() {
        Point ammo_origin{}, ammo_right{};
        Point ammo_down{};
        if (!affine_to(ammo_donor, parent, movie, {0, 0}, ammo_origin) || !affine_to(ammo_donor, parent, movie, {1, 0}, ammo_right) ||
            !affine_to(ammo_donor, parent, movie, {0, 1}, ammo_down))
            return fail_refill("hud_ammo_leaf_transform_failed", ammo_donor);
        ammo_x = {ammo_right.x - ammo_origin.x, ammo_right.y - ammo_origin.y};
        ammo_y = {ammo_down.x - ammo_origin.x, ammo_down.y - ammo_origin.y};
        const float material_width = static_cast<float>(*reinterpret_cast<uint16_t*>(ammo_glyph + 0x68));
        const float material_height = static_cast<float>(*reinterpret_cast<uint16_t*>(ammo_glyph + 0x6a));
        const float projected_width = std::fabs(ammo_x.x) * material_width + std::fabs(ammo_y.x) * material_height;
        const float projected_height = std::fabs(ammo_x.y) * material_width + std::fabs(ammo_y.y) * material_height;
        if (!(projected_width > 0 && projected_height > 0)) return fail_refill("hud_ammo_leaf_scale_invalid", ammo_glyph);
        const float plate_width = refill_plate_bounds.br.x - refill_plate_bounds.tl.x;
        const float plate_height = refill_plate_bounds.br.y - refill_plate_bounds.tl.y;
        glyph_scale = std::min({1.0f, 0.6f * plate_width / projected_width, 0.6f * plate_height / projected_height});
        Rect rendered_plate{};
        if (raw_bounds(visible_plate ? refill_plate : source_icon, rendered_plate)) {
            const Point stage_x{render_matrix[0] * ammo_x.x + render_matrix[2] * ammo_x.y, render_matrix[3] * ammo_x.x + render_matrix[1] * ammo_x.y};
            const Point stage_y{render_matrix[0] * ammo_y.x + render_matrix[2] * ammo_y.y, render_matrix[3] * ammo_y.x + render_matrix[1] * ammo_y.y};
            const float stage_width = std::fabs(stage_x.x) * material_width + std::fabs(stage_y.x) * material_height;
            const float stage_height = std::fabs(stage_x.y) * material_width + std::fabs(stage_y.y) * material_height;
            if (stage_width > 0 && stage_height > 0) glyph_scale = std::min({glyph_scale, 0.6f * (rendered_plate.br.x - rendered_plate.tl.x) / stage_width,
                              0.6f * (rendered_plate.br.y - rendered_plate.tl.y) / stage_height});
        }
        if (!std::isfinite(glyph_scale) || glyph_scale <= 0) return fail_refill("hud_ammo_leaf_scale_invalid", ammo_glyph);
        return true;
    }
    bool place_refill() {
        uintptr_t pips = child(refill, "pips");
        if (!pips) return fail_refill("hud_pips_absent", refill);
        hold_frame(pips, 3);
        three = child(pips, "pips3");
        if (!three) return fail_refill("hud_three_pip_template_absent", pips);
        show(pips, known);
        const auto desired = static_cast<uint16_t>(owner.refill_balance + 1);
        if (known && hold_frame(three, desired)) swf.dirty(three);
        const auto palette = *reinterpret_cast<int32_t*>((source == crucible_root ? widget : hammer) + 0x1ec);
        if (const auto fill = child(three, "fill")) swf.color(fill, palette);
        if (const auto fill = child(three, "innerFill")) swf.color(fill, palette);
        if (!place_visual(refill, refill_geometry, parent, movie, refill_visual_target, refill_offset)) return fail_refill("hud_refill_place_failed", refill);
        refill_anchor = {refill_visual_target.x - refill_offset.x + refill_plate_offset.x, refill_visual_target.y - refill_offset.y + refill_plate_offset.y};
        Rect rendered_glyph{};
        Point glyph_local_center{};
        if (raw_bounds(ammo_glyph, rendered_glyph) && !unrender(ammo_glyph, center(rendered_glyph), glyph_local_center)) glyph_local_center = {};
        const Point glyph_offset{ammo_x.x * glyph_local_center.x + ammo_y.x * glyph_local_center.y,
                                 ammo_x.y * glyph_local_center.x + ammo_y.y * glyph_local_center.y};
        if (!place_visual(ammo_glyph, ammo_donor, parent, movie, refill_anchor, glyph_offset, glyph_scale))
            return fail_refill("hud_ammo_leaf_place_failed", ammo_glyph);
        glyph_bounds = {};
        bounds(ammo_glyph, parent, glyph_bounds);
        show(ammo_glyph, true);
        show(refill, true);
        graphics_applied = !known || *reinterpret_cast<uint16_t*>(three + 0x58) == desired;
        return true;
    }
    bool keycap_source_ready() {
        if (!keycap_ready) {
            if (refill_bind) show(refill_bind, false);
            if (special_bind) show(special_bind, false);
            hud_trace.record(save::BStage::profile_output, save::BStatus::pending, "hud_graphics_applied_keycaps_unavailable", 0,
                             {{"owner", element},
                              {"parent", parent},
                              {"graphics_ready", graphics_ready},
                              {"keycap_ready", keycap_ready}});
            return false;
        }
        const auto donor_root = primary_root;
        donor = quickuse ? swf.keycap(quickuse) : 0;
        if (!donor_root || *reinterpret_cast<uintptr_t*>(donor_root + 0x40) != parent || !donor || !child(donor, "kbm") || !child(donor, "joy")) {
            if (refill_bind) show(refill_bind, false);
            if (special_bind) show(special_bind, false);
            refuse("hud_native_keycap_donor_unavailable", element, donor, epoch);
            return false;
        }
        return true;
    }
    bool measure_probe(uintptr_t probe, const char* label) {
        Point origin{};
        if (!affine_to(donor, parent, movie, {0, 0}, origin) || !match_linear(probe, donor, parent, movie)) {
            show(probe, false);
            refuse("hud_keycap_donor_transform_failed", element, donor, epoch);
            return false;
        }
        place(probe, origin);
        hold_frame(probe, 1);
        const auto kbm = child(probe, "kbm");
        const auto joy = child(probe, "joy");
        const auto value = text_child(kbm, "txtVal");
        if (kbm && joy && value) {
            hold_frame(kbm, 1);
            show(kbm, true);
            show(joy, false);
            if (!text_is(value, label)) swf.set_text(value, label);
            show(probe, true);
            swf.dirty(probe);
            donor_visual = bounds(kbm, parent, donor_bounds);
        }
        return true;
    }
    bool prepare_keycap_probe() {
        if (donor_visual) return true;
        const auto key = (keys & 0xff) ? keys & 0xff : (keys >> 8) & 0xff;
        auto& probe = (keys & 0xff) ? refill_bind : special_bind;
        const auto name = (keys & 0xff) ? "apAmmoRefillBind" : "apSpecialToggleBind";
        char label[16]{};
        if (!key_name(key, label)) return true;
        if (!probe) probe = clone(donor, parent, name, created);
        if (!probe) return true;
        if (!measure_probe(probe, label)) return false;
        return true;
    }
    bool probe_keycap() {
        donor_bounds = {};
        donor_visual = bounds(child(donor, "kbm"), parent, donor_bounds);
        if (!prepare_keycap_probe()) return false;
        if (!donor_visual) {
            if (!(keys & 0xff) && refill_bind) show(refill_bind, false);
            if (!((keys >> 8) & 0xff) && special_bind) show(special_bind, false);
            refuse("hud_keycap_geometry_pending", element, donor, epoch);
            return false;
        }
        return true;
    }
    bool measure_keycaps() {
        const auto donor_center = center(donor_bounds);
        const auto keycap_stage = reinterpret_cast<const float*>(parent + 0x90);
        if (!std::isfinite(keycap_stage[3]) || !std::isfinite(keycap_stage[1]) || std::fabs(keycap_stage[1]) < 1e-6f) {
            if (refill_bind) show(refill_bind, false);
            if (special_bind) show(special_bind, false);
            refuse("hud_keycap_stage_transform_failed", element, parent, epoch);
            return false;
        }
        const float keycap_row_slope = keycap_stage[3] / keycap_stage[1];
        keycap_baseline_y = donor_center.y;
        refill_key_target = {refill_anchor.x, keycap_baseline_y + (donor_center.x - refill_anchor.x) * keycap_row_slope};
        toggle_key_target = {toggle_target.x, keycap_baseline_y + (donor_center.x - toggle_target.x) * keycap_row_slope};
        bind_failure = nullptr;
        return true;
    }
    bool apply_keycaps() {
        const auto ammo_key = keys & 0xff;
        const auto toggle_key = arrow_ready ? (keys >> 8) & 0xff : 0;
        if (!bind_key(refill_bind, "apAmmoRefillBind", ammo_key, refill_key_target, "ammo_keycap_missing", "ammo_text_failed") ||
            !bind_key(special_bind, "apSpecialToggleBind", toggle_key, toggle_key_target, "toggle_keycap_missing", "toggle_text_failed")) {
            hud_trace.record(save::BStage::profile_prepare, save::BStatus::refused, bind_failure ? bind_failure : "hud_native_keycap_bind_failed", epoch,
                             {{"donor_tl", trace_point(donor_bounds.tl)},
                              {"donor_br", trace_point(donor_bounds.br)},
                              {"f9_target_x", trace_point({refill_key_target.x, 0})},
                              {"f10_target_x", trace_point({toggle_key_target.x, 0})},
                              {"keycap_baseline_y", trace_point({0, keycap_baseline_y})},
                              {"gap", trace_point({native_gap, 0})}}, element);
            refuse(bind_failure ? bind_failure : "hud_native_keycap_bind_failed", element, donor, epoch);
            return false;
        }
        return true;
    }
    bool finish() {
        if (switch_failure) {
            refuse(switch_failure, element, arrow_source, epoch);
            return false;
        }
        clips_applied = graphics_applied;
        return true;
    }
    void trace_readback() {
        Rect f9_bounds{}, f10_bounds{};
        bounds(child(refill_bind, "kbm"), parent, f9_bounds);
        if (arrow_ready) bounds(child(special_bind, "kbm"), parent, f10_bounds);
        Rect observed_special{}, observed_native_arrow{};
        Rect observed_ap_arrow{}, observed_refill{};
        Rect observed_glyph{}, observed_f9{};
        Rect observed_f10{};
        if (raw_bounds(child(active_root, "background"), observed_special) && raw_bounds(native_arrow, observed_native_arrow) &&
            raw_bounds(child(special_arrow, "arrow"), observed_ap_arrow) && raw_bounds(child(refill, "background"), observed_refill) &&
            raw_bounds(ammo_glyph, observed_glyph) && raw_bounds(child(refill_bind, "kbm"), observed_f9) &&
            raw_bounds(child(special_bind, "kbm"), observed_f10)) {
            const int64_t readback[16]{
                static_cast<int64_t>(element),         static_cast<int64_t>(owner.selected),
                trace_point(observed_special.tl),      trace_point(observed_special.br),
                trace_point(observed_native_arrow.tl), trace_point(observed_native_arrow.br),
                trace_point(observed_ap_arrow.tl),     trace_point(observed_ap_arrow.br),
                trace_point(observed_refill.tl),       trace_point(observed_refill.br), trace_point(observed_glyph.tl),        trace_point(observed_glyph.br),
                trace_point(observed_f9.tl),           trace_point(observed_f9.br), trace_point(observed_f10.tl),          trace_point(observed_f10.br)};
            static int64_t last_readback[16]{};
            static bool readback_recorded = false;
            bool changed = false;
            {
                std::lock_guard lock(state_mutex);
                changed = !readback_recorded || std::memcmp(last_readback, readback, sizeof(readback));
                if (changed) {
                    std::memcpy(last_readback, readback, sizeof(readback));
                    readback_recorded = true;
                }
            }
            if (changed) hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded, "hud_visual_render_cache_readback", epoch,
                                 {{"owner", readback[0]},
                                  {"selected", readback[1]},
                                  {"special_plate_tl", readback[2]},
                                  {"special_plate_br", readback[3]},
                                  {"native_arrow_tl", readback[4]},
                                  {"native_arrow_br", readback[5]},
                                  {"ap_arrow_tl", readback[6]},
                                  {"ap_arrow_br", readback[7]},
                                  {"refill_plate_tl", readback[8]},
                                  {"refill_plate_br", readback[9]},
                                  {"glyph_tl", readback[10]},
                                  {"glyph_br", readback[11]},
                                  {"f9_tl", readback[12]},
                                  {"f9_br", readback[13]},
                                  {"f10_tl", readback[14]},
                                  {"f10_br", readback[15]}}, element);
        }
        hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded, "hud_native_keycaps_applied", 0,
                         {{"owner", element},
                          {"parent", parent},
                          {"generation", epoch},
                          {"selected", owner.selected},
                          {"balance", owner.refill_balance},
                          {"binds", keys},
                          {"pip_frame_after", *reinterpret_cast<uint16_t*>(three + 0x58)},
                          {"ammo_source", native_ammo},
                          {"ammo_glyph", ammo_glyph},
                          {"ammo_full_path_hash", ammo_icon_path_hash},
                          {"ammo_material", attempt.material},
                          {"glyph_tl", trace_point(glyph_bounds.tl)},
                          {"glyph_br", trace_point(glyph_bounds.br)},
                          {"f9_center", trace_point(center(f9_bounds))},
                          {"f10_center", trace_point(center(f10_bounds))},
                          {"pixels_observed", 0}});
    }
};

bool project(uintptr_t element, const HudOwnerSnapshot& owner, const GraphicsSource& context, unsigned keys, uint64_t epoch, bool& clips_applied) {
    clips_applied = false;
    // engine callbacks can reenter; skip concurrent changes to the swf tree
    static std::atomic_flag presenting = ATOMIC_FLAG_INIT;
    if (presenting.test_and_set(std::memory_order_acquire)) return false;
    struct ReleasePresentation {
        std::atomic_flag& flag;
        ~ReleasePresentation() { flag.clear(std::memory_order_release); }
    } release{presenting};
    static NativePosition crucible_position{}, hammer_position{};
    static std::mutex state_mutex;
    ProjectionFrame frame{element,           owner,           context,    keys, epoch, clips_applied, crucible_position, hammer_position, state_mutex};
    if (!frame.prepare()) return false;
    if (!frame.create_clips()) return false;
    frame.read_special_geometry();
    frame.measure_layout();
    frame.measure_refill();
    frame.find_switch_failure();
    frame.place_switch();
    frame.trace_layout();
    if (!frame.refill_geometry_ready()) return false;
    if (!frame.prepare_ammo()) return false;
    if (!frame.apply_ammo_material()) return false;
    if (!frame.measure_ammo()) return false;
    if (!frame.place_refill()) return false;
    if (!frame.keycap_source_ready()) return frame.graphics_applied;
    if (!frame.probe_keycap()) return frame.graphics_applied;
    if (!frame.measure_keycaps()) return frame.graphics_applied;
    if (!frame.apply_keycaps()) return frame.graphics_applied;
    if (!frame.finish()) return frame.graphics_applied;
    frame.trace_readback();
    return frame.graphics_applied;
}
void project_progress(uintptr_t parent, uintptr_t anchor, const combat::ProgressView& progress) {
    if (!graphics_ready || !parent) return;
    auto value = child(parent, "apCombatProgress");
    const auto hide = [&] { if (value) show(value, false); };
    if (!anchor || !progress.visible || !progress.target) { hide(); return; }
    Rect rect{};
    Point origin{};
    if (!bounds(anchor, parent, rect) || !position(anchor, origin)) { hide(); return; }
    bool created = false;
    value = clone(anchor, parent, "apCombatProgress", created);
    if (!value) return;
    if (created) hold_frame(value, 1);
    const auto text = text_child(child(value, "text"), "txtVal");
    if (!text) { hide(); return; }
    if (const auto image = child(value, "question")) show(image, false);
    const auto movie = *reinterpret_cast<uintptr_t*>(parent + 0x30);
    if (!match_linear(value, anchor, parent, movie)) { hide(); return; }
    place(value, {origin.x, origin.y + (rect.br.y - rect.tl.y) * 1.25f});
    char label[80]{};
    std::snprintf(label, sizeof(label), player_strings::hud_kills_progress, progress.total, progress.target);
    if (!text_is(text, label)) { swf.set_text(text, label); swf.dirty(value); }
    show(value, true);
}
}
std::atomic<uintptr_t> challenge_element{0};
std::atomic<uintptr_t> completion_owner{0};
std::atomic<uintptr_t> challenge_player{0};
std::atomic<uintptr_t> weapon_info_element{0};
std::atomic<uintptr_t> weapon_info_player{0};
std::atomic<uint64_t> weapon_info_epoch{0};
thread_local bool weapon_info_update_phase = false;
struct HudCounts { uintptr_t owner = 0; uint64_t epoch = 0; uint32_t attempts = 0, presentations = 0; };
std::mutex hud_counts_mutex;
HudCounts hud_counts;
HudCounts note_hud_update(uintptr_t owner, uint64_t epoch, bool presented) {
    std::lock_guard lock(hud_counts_mutex);
    if (hud_counts.owner != owner || hud_counts.epoch != epoch) hud_counts = {owner, epoch};
    auto& count = presented ? hud_counts.presentations : hud_counts.attempts;
    if (count != UINT32_MAX) ++count;
    return hud_counts;
}
uintptr_t player(void*);
#ifdef SC_NATIVE_TESTING
uintptr_t test_hud_player = 0;
EarningsAppend test_earnings_append = nullptr;
bool (*test_selection_read)(uintptr_t, SnapshotFacts&) = nullptr;
#endif

uintptr_t current_hud_player() {
#ifdef SC_NATIVE_TESTING
    if (test_hud_player) return test_hud_player;
#endif
    return player(nullptr);
}

struct LabelProjection { uintptr_t element = 0; uint64_t epoch = 0; const char* label = nullptr; };
// shortcut: MSVC 14.44 breaks optimized SEH here; retest on compiler upgrade.
#pragma optimize("", off)
LabelProjection project_weapon_label(uintptr_t element, uint64_t epoch, uintptr_t current_player, const char (&namespace_id)[65], uintptr_t parent) {
    if (element && epoch && epoch == native::observation_stamp() && epoch == weapon_info_epoch.load(std::memory_order_acquire) && current_player &&
        current_player == weapon_info_player.load(std::memory_order_acquire) && current_player == current_hud_player() &&
        *reinterpret_cast<uintptr_t*>(element) == image_base + rva_weapon_info_vtable &&
        *reinterpret_cast<uint8_t*>(element + 0x209) && hud_owner_snapshot(namespace_id).namespace_valid &&
        inventory::admitted(namespace_id) && inventory::grenade_indicator_mask(current_player) == 2 && hud::graphics_source(element).parent == parent)
        return {element, epoch, *reinterpret_cast<uint8_t*>(element + 0x2a9) ? "cursed" : "normal"};
    return {};
}
__declspec(noinline) LabelProjection label_projection(uintptr_t parent, const char* label, uintptr_t caller) {
    __try {
        if ((caller != 0xf0cb96 && caller != 0xf0c314 && caller != 0xf0c34a) || std::strcmp(label, "noEquipment")) return {};
        const auto element = weapon_info_element.load(std::memory_order_acquire);
        const auto epoch = route_epoch.load(std::memory_order_acquire);
        const auto current_player = route_player.load(std::memory_order_acquire);
        char namespace_id[65];
        AcquireSRWLockShared(&route_namespace_lock);
        std::memcpy(namespace_id, route_namespace, sizeof(namespace_id));
        ReleaseSRWLockShared(&route_namespace_lock);
        return project_weapon_label(element, epoch, current_player, namespace_id, parent);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ready.store(false, std::memory_order_release);
        hud_trace.record(save::BStage::root_layout, save::BStatus::refused, "weapon_label_fault", 0, {{"exception", GetExceptionCode()}});
    }
    return {};
}
#pragma optimize("", on)
uint64_t weapon_info_label_detour(uintptr_t parent, const char* label, bool warn, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller) - image_base;
    const auto projection = label_projection(parent, label, caller);
    if (projection.label) {
        const auto desired = original_weapon_info_label(parent, projection.label, warn);
        const auto absent = original_weapon_info_label(parent, label, false);
        if (desired > 0 && desired != absent) {
            hud_trace.record(save::BStage::root_layout, save::BStatus::succeeded, "hud_ice_only_timeline_preserved", projection.epoch,
                {{"owner", projection.element}, {"parent", parent}, {"native_frame", absent},
                 {"projected_frame", desired}, {"grenade_mask", 2}}, projection.element);
            return desired;
        }
    }
    return original_weapon_info_label(parent, label, warn);
}

struct SpecialHudFrame {
    uintptr_t element{};
    uint64_t epoch{};
    bool context_valid{};
    char namespace_id[65]{};
    HudOwnerSnapshot owner{};
    bool crucible{}, hammer{};
    bool valid{};
    uintptr_t current_player{}, native_player{};
    uintptr_t observed_vtable{};
    bool hud_valid{};
    hud::GraphicsSource graphics{};
    bool source_evaluated{};
    unsigned keys{};
    uint32_t refusal{};
    uintptr_t primary_icon{};
    uint8_t grenade_mask{};
    uintptr_t quickuse_selected{};
    uint16_t parent_frame{};
};
void trace_weapon_context(SpecialHudFrame& frame) {
    const auto primary_visible = frame.graphics.primary_root ? *reinterpret_cast<uint8_t*>(frame.graphics.primary_root + 0x51) : uint8_t{255};
    const auto icon_visible = frame.primary_icon ? *reinterpret_cast<uint8_t*>(frame.primary_icon + 0x51) : uint8_t{255};
    const auto primary_frame = frame.graphics.primary_root ? *reinterpret_cast<uint16_t*>(frame.graphics.primary_root + 0x58) : uint16_t{0};
    const auto icon_frame = frame.primary_icon ? *reinterpret_cast<uint16_t*>(frame.primary_icon + 0x58) : uint16_t{0};
    const auto parent_visible = frame.graphics.parent ? *reinterpret_cast<uint8_t*>(frame.graphics.parent + 0x51) : uint8_t{255};
    frame.parent_frame = frame.graphics.parent ? *reinterpret_cast<uint16_t*>(frame.graphics.parent + 0x58) : uint16_t{0};
    static thread_local std::array<uint64_t, 25> previous_context{};
    const std::array<uint64_t, 25> context{frame.element, frame.observed_vtable, frame.current_player, frame.epoch,
        frame.owner.revision, frame.owner.request_revision, frame.keys, frame.refusal, hud::graphics_ready,
        hud::keycap_ready, frame.source_evaluated, frame.owner.namespace_valid, GetCurrentThreadId(), route_thread.load(std::memory_order_acquire),
        frame.grenade_mask, frame.quickuse_selected, primary_visible, icon_visible,
        frame.graphics.primary_root, frame.primary_icon, frame.graphics.parent, primary_frame, icon_frame, parent_visible, frame.parent_frame};
    if (context != previous_context) {
        previous_context = context;
        hud_trace.record(frame.refusal ? save::BStage::profile_choice : save::BStage::profile_read,
            frame.refusal ? save::BStatus::refused : save::BStatus::entered, "hud_weapon_info_update_context", 0,
            {{"weapon_info_owner", frame.element}, {"player", frame.current_player},
              {"observation_epoch", frame.epoch}, {"refusal", frame.refusal},
              {"grenade_mask", frame.grenade_mask}, {"quickuse_selected", frame.quickuse_selected},
              {"primary_visible", primary_visible}, {"icon_visible", icon_visible},
              {"primary", frame.graphics.primary_root}, {"icon", frame.primary_icon}, {"parent", frame.graphics.parent},
              {"primary_frame", primary_frame}, {"icon_frame", icon_frame},
              {"parent_visible", parent_visible}, {"parent_frame", frame.parent_frame}});
    }
}
void present_native_special(SpecialHudFrame& frame) {
    const bool native_special_widgets = weapon_info_update_phase &&
        frame.observed_vtable == image_base + rva_weapon_info_vtable && *reinterpret_cast<uintptr_t*>(frame.element + 0x1e8) &&
        *reinterpret_cast<uintptr_t*>(frame.element + 0x1f8);
    if (native_special_widgets && (!frame.valid || (!frame.crucible && !frame.hammer))) {
        project_crucible_hud(frame.element);
        project_hammer_hud(frame.element);
    }
    else if (native_special_widgets) {
        auto& visible_c = *reinterpret_cast<uint8_t*>(frame.element + 0x169);
        auto& visible_h = *reinterpret_cast<uint8_t*>(frame.element + 0x1b9);
        const auto original_c = visible_c, original_h = visible_h;
        const bool show_c = frame.crucible && *reinterpret_cast<int32_t*>(frame.element + 0x170) > 0;
        const bool show_h = frame.hammer && original_h;
        __try {
            visible_c = show_c;
            visible_h = show_h;
            project_crucible_hud(frame.element);
            project_hammer_hud(frame.element);
        } __finally {
            visible_c = original_c;
            visible_h = original_h;
        }
        installation_trace.record(save::BStage::profile_output, save::BStatus::succeeded, "special_hud_projection_submitted", 0,
            {{"policy", frame.owner.selected}, {"crucible_visible", show_c},
             {"hammer_visible", show_h}, {"pixels_observed", 0}});
    }
}
void restore_ice_indicator(SpecialHudFrame& frame) {
    if (inventory::admitted(frame.namespace_id) && hud::graphics_ready && frame.graphics.primary_root &&
        frame.graphics.movie && *reinterpret_cast<uintptr_t*>(frame.graphics.primary_root + 0x30) == frame.graphics.movie &&
        *reinterpret_cast<uintptr_t*>(frame.graphics.primary_root + 0x40) == frame.graphics.parent &&
        frame.grenade_mask == 2 && *reinterpret_cast<uint8_t*>(frame.element + 0x209)) {
        // native weaponinfo chooses noequipment from frag ownership alone. ice-only needs the equipment timeline before ap clips are placed
        const bool frame_changed = hud::hold_ice_frame(frame.graphics.parent, *reinterpret_cast<uint8_t*>(frame.element + 0x2a9) != 0);
        const bool visibility_changed = !*reinterpret_cast<uint8_t*>(frame.graphics.primary_root + 0x51) ||
            (frame.primary_icon && !*reinterpret_cast<uint8_t*>(frame.primary_icon + 0x51));
        hud::show(frame.graphics.primary_root, true);
        if (frame.primary_icon) hud::show(frame.primary_icon, true);
        if (frame_changed || visibility_changed) hud_trace.record(save::BStage::root_layout, save::BStatus::succeeded,
            "hud_ice_only_indicator_restored", frame.epoch,
            {{"owner", frame.element}, {"primary", frame.graphics.primary_root},
             {"icon", frame.primary_icon}, {"parent", frame.graphics.parent}, {"parent_frame_before", frame.parent_frame},
             {"parent_frame_after", *reinterpret_cast<uint16_t*>(frame.graphics.parent + 0x58)},
             {"grenade_mask", frame.grenade_mask}, {"quickuse_selected", frame.quickuse_selected}}, frame.element);
        if (frame_changed) frame.graphics = hud::graphics_source(frame.element);
    }
}
void trace_projected_context(SpecialHudFrame& frame) {
    if (frame.source_evaluated) {
        const auto final_graphics = hud::graphics_source(frame.element);
        const auto final_icon = final_graphics.primary_root ? hud::child(final_graphics.primary_root, "icon") : 0;
        const uintptr_t clips[3]{final_graphics.primary_root, final_icon, final_graphics.parent};
        std::array<uint64_t, 13> final_context{frame.element, frame.epoch, frame.grenade_mask, *reinterpret_cast<uintptr_t*>(frame.element + 0x178)};
        for (unsigned i = 0; i < 3; ++i) {
            final_context[4 + i * 3] = clips[i];
            final_context[5 + i * 3] = clips[i] ? *reinterpret_cast<uint8_t*>(clips[i] + 0x51) : 255;
            final_context[6 + i * 3] = clips[i] ? *reinterpret_cast<uint16_t*>(clips[i] + 0x58) : 0;
        }
        static thread_local std::array<uint64_t, 13> previous_final{};
        if (final_context != previous_final) {
            previous_final = final_context;
            hud_trace.record(save::BStage::profile_publish, save::BStatus::entered, "hud_weapon_info_after_presenters", frame.epoch,
                {{"owner", frame.element}, {"grenade_mask", frame.grenade_mask}, {"quickuse_selected", final_context[3]},
                 {"primary", final_context[4]}, {"primary_visible", final_context[5]}, {"primary_frame", final_context[6]},
                 {"icon", final_context[7]}, {"icon_visible", final_context[8]}, {"icon_frame", final_context[9]},
                 {"parent", final_context[10]}, {"parent_visible", final_context[11]}, {"parent_frame", final_context[12]}}, frame.element);
        }
    }
}
uint32_t hud_context_refusal(const SpecialHudFrame& frame) {
    return !frame.valid ? 1 : !weapon_info_update_phase ? 6 :
        !frame.current_player || frame.native_player != frame.current_player ? 2 : !frame.owner.namespace_valid ? 5 :
        frame.observed_vtable != image_base + rva_weapon_info_vtable ? 4 : 0;
}
bool is_weapon_hud_owner(const SpecialHudFrame& frame) {
    return frame.valid && weapon_info_update_phase && frame.element && frame.current_player &&
        frame.native_player==frame.current_player && frame.owner.namespace_valid && frame.observed_vtable==image_base+rva_weapon_info_vtable;
}
void sample_hud_context(SpecialHudFrame& frame) {
    frame.valid = frame.context_valid && frame.epoch == native::observation_stamp();
    frame.current_player = route_player.load(std::memory_order_acquire);
    frame.native_player = current_hud_player();
    frame.observed_vtable = frame.element ? *reinterpret_cast<uintptr_t*>(frame.element) : 0;
    if (frame.observed_vtable == image_base + rva_weapon_info_vtable) note_hud_update(frame.element, frame.epoch, false);
    frame.hud_valid=is_weapon_hud_owner(frame);
    frame.source_evaluated = frame.hud_valid && hud::graphics_ready;
    if (frame.source_evaluated) frame.graphics = hud::graphics_source(frame.element);
    frame.keys = configured_keys.load(std::memory_order_relaxed);
    frame.refusal=hud_context_refusal(frame);
    frame.primary_icon = frame.graphics.primary_root ? hud::child(frame.graphics.primary_root, "icon") : 0;
    frame.grenade_mask=255;
    if (frame.hud_valid && inventory::admitted(frame.namespace_id)) frame.grenade_mask=inventory::grenade_indicator_mask(frame.current_player);
    frame.quickuse_selected = frame.hud_valid ? *reinterpret_cast<uintptr_t*>(frame.element + 0x178) : 0;
}
void apply_special_hud(SpecialHudFrame& frame) {
    if (frame.hud_valid) {
        weapon_info_element.store(frame.element, std::memory_order_release);
        weapon_info_player.store(frame.current_player, std::memory_order_release);
        weapon_info_epoch.store(frame.epoch, std::memory_order_release);
        restore_ice_indicator(frame);
        bool clips_applied = false;
        hud::project(frame.element, frame.owner, frame.graphics, frame.keys, frame.epoch, clips_applied);
        if (clips_applied) {
            note_hud_update(frame.element, frame.epoch, true);
            hud_trace.record(save::BStage::profile_capture, save::BStatus::succeeded, "hud_projection_clips_applied", 0,
                {{"weapon_info_owner", frame.element}, {"parent", frame.graphics.parent},
                 {"movie", frame.graphics.movie}, {"observation_epoch", frame.epoch},
                 {"callback_thread", GetCurrentThreadId()},
                 {"publisher_thread", route_thread.load(std::memory_order_acquire)},
                 {"snapshot_revision", frame.owner.revision}, {"balance", frame.owner.refill_balance},
                 {"keys", frame.keys}, {"pixels_observed", 0}});
        }
    }
    else {
        weapon_info_element.store(0, std::memory_order_release);
        weapon_info_player.store(0, std::memory_order_release);
        weapon_info_epoch.store(0, std::memory_order_release);
    }
}
void project_special_hud(uintptr_t element) {
    SpecialHudFrame frame{};
    frame.element=element;
    frame.epoch = route_epoch.load(std::memory_order_acquire);
    frame.context_valid = frame.epoch && frame.epoch == native::observation_stamp();
    AcquireSRWLockShared(&route_namespace_lock);
    std::memcpy(frame.namespace_id, route_namespace, sizeof(frame.namespace_id));
    ReleaseSRWLockShared(&route_namespace_lock);
    frame.owner = hud_owner_snapshot(frame.namespace_id);
    frame.crucible = frame.owner.selected == SC_SPECIAL_WEAPON_CRUCIBLE && frame.owner.owns_crucible;
    frame.hammer = frame.owner.selected == SC_SPECIAL_WEAPON_HAMMER && frame.owner.owns_hammer;
    __try {
        sample_hud_context(frame);
        trace_weapon_context(frame);
        present_native_special(frame);
        apply_special_hud(frame);
        trace_projected_context(frame);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        installation_trace.record(save::BStage::profile_output, save::BStatus::refused, "special_hud_projection_fault", 0, {{"exception", GetExceptionCode()}});
    }
}
void weapon_hud_update_detour(uintptr_t element, uintptr_t time) {
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    original_weapon_hud_update(element, time);
    const auto native_done=Clock::now();
    const bool previous = weapon_info_update_phase;
    weapon_info_update_phase = true;
    project_special_hud(element);
    weapon_info_update_phase = previous;
    static std::atomic<uint64_t> ticks{0},native_us{0},ap_us{0},next{0};
    ++ticks;
    native_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(native_done-start).count());
    ap_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-native_done).count());
    const auto now=GetTickCount64();
    auto due=next.load();
    if (now>=due && next.compare_exchange_strong(due,now+5000))
        save::session().btrace.record(save::BStage::weapon_hud_cost,save::BStatus::succeeded,"weapon_hud_cost_us",0,
            {{"calls",ticks.load()},{"native_us",native_us.load()},{"ap_us",ap_us.load()}});
}
uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        return map ? reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t)>(image_base + rva_player)(map, 0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t inventory_of(uintptr_t p) {
    if (!p) return 0;
    const auto vtable = *reinterpret_cast<uintptr_t**>(p);
    if (!vtable) return 0;
    const auto get_inventory = reinterpret_cast<uintptr_t(*)(uintptr_t)>(vtable[0x540 / 8]);
    return get_inventory ? get_inventory(p) : 0;
}
const char* decl_path(uintptr_t decl) {
    if (!decl) return nullptr;
    __try { return *reinterpret_cast<const char**>(decl + 8); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
uintptr_t find_decl(const char* path) {
    if (!path) return 0;
    const auto typeinfo = reinterpret_cast<uintptr_t(*)()>(image_base + rva_inventory_typeinfo)();
    if (!typeinfo) return 0;
    return reinterpret_cast<FindDecl>(image_base + rva_find_decl)(typeinfo, path, 1);
}
uintptr_t find_perk_decl(const char* path) {
    if (!path) return 0;
    const auto typeinfo = reinterpret_cast<uintptr_t(*)()>(image_base + rva_perk_typeinfo)();
    if (!typeinfo) return 0;
    const auto decl = reinterpret_cast<FindDecl>(image_base + rva_find_decl)(typeinfo, path, 1);
    const auto resolved = decl_path(decl);
    return resolved && std::strcmp(resolved, path) == 0 ? decl : 0;
}
bool hammer_loot_modifier(uintptr_t p, bool& effective) {
    const auto handler = p + equipment_upgrade_offset;
    if (*reinterpret_cast<uintptr_t*>(handler) != image_base + rva_equipment_upgrade_vtable) return false;
    effective = *reinterpret_cast<uintptr_t*>(handler + hammer_loot_modifier_offset) != 0;
    return true;
}
bool hammer_perks(uintptr_t p, uintptr_t (&decls)[3], uint8_t& effective_count) {
    for (size_t i = 0; i < 3; ++i) {
        decls[i] = find_perk_decl(HAMMER_PERK_PATHS[i]);
        if (!decls[i]) return false;
    }
    bool modifier_effective = false;
    if (!hammer_loot_modifier(p, modifier_effective)) return false;
    const auto component = p + perk_component_offset;
    const auto active = reinterpret_cast<ActivePerk>(image_base + rva_active_perk);
    uint8_t active_count = 0;
    for (const auto decl : decls) active_count += active(component, decl) ? 1 : 0;
    effective_count = modifier_effective ? active_count : 0;
    return true;
}
uintptr_t find_item(uintptr_t inventory, uintptr_t decl) {
    if (!inventory || !decl) return 0;
    return reinterpret_cast<FindItem>(image_base + rva_find_item)(inventory, decl);
}

// match the native resolver's typed presence checks, independently of charges
bool route_ownership(uintptr_t p, bool& crucible, bool& hammer) {
    __try {
        const auto c = *reinterpret_cast<uintptr_t*>(p + 0x4d198);
        const auto h = *reinterpret_cast<uintptr_t*>(p + 0x4d1d8);
        if (!c || !h) return false;
        const auto table = *reinterpret_cast<uintptr_t*>(p);
        const auto inv = reinterpret_cast<uintptr_t(*)(uintptr_t)>( *reinterpret_cast<uintptr_t*>(table + 0x538))(p);
        if (!inv) return false;
        const auto cast = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + rva_weapon_cast);
        crucible = cast(find_item(inv, c)) != 0;
        hammer = cast(find_item(inv, h)) != 0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint32_t route_scope_reason(uintptr_t p) {
    if (!route_ready.load(std::memory_order_acquire)) return 1;
    const auto epoch = route_epoch.load(std::memory_order_acquire);
    if (!epoch || epoch != native::observation_stamp()) return 7;
    if (GetCurrentThreadId() != route_thread.load(std::memory_order_acquire) && (input_job_player != p || input_job_epoch != epoch)) return 2;
    if (!p || p != route_player) return 3;
    if (!native::gameplay_admitted()) return 4;
    if (p != player(nullptr)) return 5;
    AcquireSRWLockShared(&route_namespace_lock);
    const bool same_namespace = !std::memcmp(route_namespace, save::session().namespace_id().c_str(), 65);
    ReleaseSRWLockShared(&route_namespace_lock);
    if (!same_namespace) return 6;
    return 0;
}
uint32_t applied_mode(bool crucible, bool hammer) {
    if (crucible && hammer) return route_requested;
    return crucible ? SC_SPECIAL_WEAPON_CRUCIBLE : hammer ? SC_SPECIAL_WEAPON_HAMMER : SC_SPECIAL_WEAPON_NONE;
}
void publish_policy(uintptr_t p) {
    // only the admitted owner/job samples mutable engine inventory
    if (route_scope_reason(p)) return;
    bool c = false, h = false;
    const bool known = route_ownership(p, c, h);
    AcquireSRWLockExclusive(&route_namespace_lock);
    if (known && route_requested == SC_SPECIAL_WEAPON_NONE)
        route_requested = h ? SC_SPECIAL_WEAPON_HAMMER : c ? SC_SPECIAL_WEAPON_CRUCIBLE : SC_SPECIAL_WEAPON_NONE;
    published_policy = {p, route_epoch.load(), known ? applied_mode(c, h) : 0, c, h, known};
    std::memcpy(published_policy.namespace_id, route_namespace, 65);
    ReleaseSRWLockExclusive(&route_namespace_lock);
}
uint32_t policy_snapshot(uintptr_t p, PolicySnapshot& snapshot) {
    if (!route_ready.load(std::memory_order_acquire)) return 1;
    if (!native::gameplay_admitted()) return 4;
    const auto epoch = native::observation_stamp();
    AcquireSRWLockShared(&route_namespace_lock);
    snapshot = published_policy;
    const bool same_namespace = !std::memcmp(route_namespace, snapshot.namespace_id, 65);
    ReleaseSRWLockShared(&route_namespace_lock);
    if (!same_namespace || !hud_owner_snapshot(snapshot.namespace_id).namespace_valid) return 6;
    if (!snapshot.epoch || snapshot.epoch != epoch || epoch != native::observation_stamp()) return 7;
    if (!p || snapshot.player != p) return 3;
    return snapshot.known ? 0 : 8;
}

__declspec(noinline) bool crucible_resolver_detour(uintptr_t p, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
    PolicySnapshot snapshot{};
    const auto reason = policy_snapshot(p, snapshot);
    const bool known = !reason;
    const bool c = snapshot.crucible, h = snapshot.hammer;
    const auto mode = known ? snapshot.selected : SC_SPECIAL_WEAPON_NONE;
    const bool result = known ? c && mode == SC_SPECIAL_WEAPON_CRUCIBLE : original_crucible_resolver(p);
    record_use("crucible_resolver", known ? save::BStatus::succeeded : save::BStatus::refused,
        {{"caller_rva", caller - image_base}, {"player", p}, {"scope_reason", reason},
         {"owner_thread", route_thread.load()}, {"presence_known", known}, {"crucible_present", c},
         {"hammer_present", h}, {"policy_selected", mode}, {"gate_result", result},
         {"vanilla_fallback", !known}, {"action_observed", 0}});
    // resource/action guards remain in the original callers and activatecrucible
    return result;
}

struct RouteObservation {
    uintptr_t held = 0;
    uint32_t known = 0, charge = UINT32_MAX, meter_milli = UINT32_MAX, action = UINT32_MAX, error = 0;
    uint32_t component20_bits = 0, component28_bits = 0, effect68 = UINT32_MAX, effect80 = UINT32_MAX;
    int32_t pending_quick_slot = -1;
};
RouteObservation observe_route(uintptr_t p);

void record_hammer_transition(uintptr_t caller,bool down,uint32_t reason,uint32_t mode,bool c,bool h) {
    const bool allowed = h && mode == SC_SPECIAL_WEAPON_HAMMER;
    static uint64_t input_sequence = 0;
    if (down) ++input_sequence;
    const auto observed = !reason ? observe_route(route_player) : RouteObservation{};
    record_use("special_input_transition", reason || observed.error ? save::BStatus::refused : save::BStatus::succeeded,
        {{"caller_rva", caller - image_base}, {"down", down}, {"policy_selected", mode},
         {"held_decl", observed.held}, {"hands_action", observed.action},
         {"resource_known", observed.known}, {"crucible_charge", observed.charge},
         {"hammer_meter_milli", observed.meter_milli}, {"error", observed.error}});
    route_trace.record(save::BStage::special_input, save::BStatus::succeeded, "core_policy_input_gate", GetTickCount64(),
        {{"down", down}, {"requested", route_requested.load()}, {"applied", mode}, {"hammer_allowed", allowed},
         {"crucible_owned", c}, {"hammer_owned", h}, {"caller_rva", rva_hammer_input_return},
         {"input_sequence", input_sequence}, {"native_known", observed.known},
         {"held_decl", observed.held}, {"crucible_charge", observed.charge},
         {"hammer_meter_milli", observed.meter_milli}, {"hands_action", observed.action},
         {"observation_error", observed.error}, {"native_handler_return_known", 0}});
}
bool observe_hammer_button(bool site,bool raw) {
    const bool prior=site ? special_down.exchange(raw) : special_down.load();
    if (site && raw && !prior) begin_use_attempt();
    return prior;
}
bool filter_hammer_input(uintptr_t caller, uint64_t button, bool raw, uintptr_t input = 0) {
    if (caller != image_base + rva_hammer_input_return && button != special_button) return raw;
    const auto reason = route_scope_reason(route_player);
    const bool site = caller == image_base + rva_hammer_input_return && button == special_button;
    const bool prior_down=observe_hammer_button(site,raw);
    bool c = false, h = false;
    const bool known = site && !reason && route_ownership(route_player, c, h);
    const auto selected = known ? applied_mode(c, h) : SC_SPECIAL_WEAPON_NONE;
    const bool hammer_selected=h && selected==SC_SPECIAL_WEAPON_HAMMER;
    const bool result=known ? raw && hammer_selected : raw;
    record_use("hammer_input", known ? save::BStatus::succeeded : save::BStatus::refused,
        {{"caller_rva", caller - image_base}, {"input", input}, {"mask", button}, {"raw_al", raw},
         {"scope_reason", reason}, {"owner_thread", route_thread.load()}, {"caller_mask_match", site},
         {"presence_known", known}, {"crucible_present", c}, {"hammer_present", h},
         {"policy_selected", selected}, {"gate_result", result}, {"vanilla_fallback", !known},
         {"action_observed", 0}});
    if (site && raw!=prior_down) record_hammer_transition(caller,raw,reason,selected,c,h);
    // query before the hammer meter, latch and animation; keep other callers' results and don't invent an input
    return result;
}

__declspec(noinline) bool input_down_detour(uintptr_t input, uint64_t button, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
    return filter_hammer_input(caller, button, original_input_down(input, button), input);
}
uintptr_t give_item(uintptr_t inventory, uintptr_t p, uintptr_t decl, int count, uint8_t count_is_amount) {
    if (!inventory || !p || !decl || count < 1) return 0;
    // acquisition flags do not guarantee ammo or selection preservation
    return reinterpret_cast<GiveItem>(image_base + rva_give_item)( inventory, p, decl, count, count_is_amount, 0, 1, 0);
}
uintptr_t current_weapon_decl(uintptr_t p) {
    if (!p) return 0;
    const auto weapon = reinterpret_cast<CurrentWeapon>(image_base + rva_current_weapon)(p);
    if (!weapon) return 0;
    __try { return *reinterpret_cast<uintptr_t*>(weapon + 0x38); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t restorable_weapon(uintptr_t inv,uintptr_t decl,int32_t count,int16_t& subweb) {
    const auto weapon = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + rva_weapon_cast)(find_item(inv, decl));
    if (!weapon || !*reinterpret_cast<uint8_t*>(weapon + 0x47) || *reinterpret_cast<uint8_t*>(weapon + 0x23ca)) return 0;
    const auto mode = *reinterpret_cast<int32_t*>(weapon + 0x19e8);
    if (mode < 0) return 0;
    const auto reflection = reinterpret_cast<uintptr_t(*)(uintptr_t, int32_t)>( image_base + rva_weapon_mode_decl)(weapon, mode);
    if (!reflection || !*reinterpret_cast<uint8_t*>(reflection + 0xbe9) ||
        !*reinterpret_cast<uint8_t*>(reflection + 0xbea) || !*reinterpret_cast<uint8_t*>(reflection + 0xbeb)) return 0;
    auto index = *reinterpret_cast<int16_t*>(weapon + 0x190 + static_cast<uintptr_t>(mode) * 36);
    if (index < 0) index = *reinterpret_cast<int16_t*>(weapon + 0x190);
    if (index < 0 || index >= count) return 0;
    subweb = index;
    return weapon;
}
uintptr_t hands_restore_weapon(uintptr_t p, int16_t& subweb, uint32_t& ordinal) {
    const char* const paths[] = {
        "weapon/player/shotgun", "weapon/player/heavy_cannon", "weapon/player/plasma_rifle", "weapon/player/rocket_launcher", "weapon/player/double_barrel",
        "weapon/player/gauss_rifle", "weapon/player/chaingun"
    };
    const auto hands = p + 0xd2c8;
    if (!hands || *reinterpret_cast<uintptr_t*>(hands + 0x358) != p) return 0;
    const auto table = *reinterpret_cast<uintptr_t*>(hands + 0x2f8);
    const auto count = *reinterpret_cast<int32_t*>(hands + 0x300);
    if (!table || count <= 0 || count > INT16_MAX) return 0;
    const auto inv = inventory_of(p);
    if (!inv) return 0;
    const auto current = current_weapon_decl(p);
    for (unsigned pass = 0; pass < 2; ++pass) for (uint32_t i = 0; i < std::size(paths); ++i) {
        const auto decl = find_decl(paths[i]);
        if (!decl || (pass == 0 ? decl != current : decl == current)) continue;
        const auto weapon=restorable_weapon(inv,decl,count,subweb);
        if (!weapon) continue;
        ordinal = i;
        return weapon;
    }
    return 0;
}
uintptr_t restore_hands_guarded(uintptr_t p, bool checkpoint, uintptr_t caller, int16_t& subweb, uint32_t& ordinal) {
    uintptr_t fallback = 0;
    __try {
        if (p && (checkpoint || caller != rva_hands_init_return || *reinterpret_cast<uint8_t*>(p + 0x4ea2)))
            fallback = hands_restore_weapon(p, subweb, ordinal);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return fallback;
}
__declspec(noinline) uintptr_t best_weapon_detour(uintptr_t p, bool exclude_current, uintptr_t excluded, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller) - image_base;
    const auto selected = original_best_weapon(p, exclude_current, excluded);
    if (selected || exclude_current || excluded || (caller != rva_hands_reset_return && caller != rva_hands_refresh_return && caller != rva_hands_init_return))
        return selected;
    if (save::session().state() != save::SessionState::admitted || !save::session().accepts_requests()) return selected;
    const bool checkpoint = native::checkpoint_loading();
    if (!checkpoint && route_scope_reason(p)) return selected;
    uintptr_t fallback = 0;
    int16_t subweb = -1;
    uint32_t ordinal = UINT32_MAX;
    fallback = restore_hands_guarded(p, checkpoint, caller, subweb, ordinal);
    record_use("hands_restore_fallback", fallback ? save::BStatus::succeeded : save::BStatus::refused,
        {{"caller_rva", caller}, {"checkpoint", checkpoint}, {"subweb", static_cast<uint16_t>(subweb)},
         {"weapon_ordinal", ordinal}, {"epoch", native::observation_stamp()}});
    return fallback;
}
uintptr_t best_weapon_detour_entry(uintptr_t p, bool exclude_current, uintptr_t excluded) {
    const auto caller = _ReturnAddress();
    const auto lease = hands_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_best_weapon)>(lease.original)(p, exclude_current, excluded);
        return best_weapon_detour(p, exclude_current, excluded, caller);
    } __finally { hands_hooks.leave(lease); }
}
bool validate_hands_site(const builds::HandsSite& site,const engine::Binding& binding,engine::LocalMemory& memory,HANDLE stop) {
    const auto target=native::target_from_hex(image_base+site.rva,site.bytes);
    std::array<uint8_t, 32> actual{};
    const bool section = binding.image.contains(site.rva, actual.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE);
    const bool valid = section && !memory.copy(target.address, actual.data(), actual.size()).reason &&
        actual == target.bytes && (!site.owner || native::function_window(memory, binding.image, image_base + site.owner, target.address, site.call_length));
    const auto validation = !valid ? SC_NATIVE_TARGET_BYTES : site.rva == rva_best_weapon ?
        native::validate_target(memory, binding.image, target, stop, GetTickCount64() + 10000) : SC_NATIVE_NONE;
    installation_trace.record(save::BStage::native_start, validation ? save::BStatus::refused : save::BStatus::entered, "hands_restore_site", 0,
        {{"rva", site.rva}, {"validation", validation}});
    return !validation;
}
void install_hands_restore(const engine::Binding& binding, HANDLE stop) {
    engine::LocalMemory memory;
    const auto& sites = builds::steam_20260818().hands;
#ifdef SC_NATIVE_TESTING
    if (testing::real_hook_targets())
#endif
    for (const auto& site : sites) { if (!validate_hands_site(site,binding,memory,stop)) return; }
    const hooks::HookSpec specs[]{hooks::bind(image_base + rva_best_weapon, best_weapon_detour_entry, original_best_weapon)};
    const bool installed = hands_hooks.install(specs, stop);
    installation_trace.record(save::BStage::native_start, installed ? save::BStatus::succeeded : save::BStatus::refused, "hands_restore_install", 0,
        {{"rva", rva_best_weapon}, {"installed", installed}});
}
uint32_t item_count(uintptr_t item) {
    if (!item) return 0;
    const auto vtable = *reinterpret_cast<uintptr_t**>(item);
    if (!vtable) return 0;
    const auto count = reinterpret_cast<int(*)(uintptr_t)>(vtable[0xd0 / 8]);
    if (!count) return 0;
    const auto value = count(item);
    return value > 0 ? static_cast<uint32_t>(value) : 0;
}
uint32_t refill_capacity(uintptr_t item) noexcept {
    __try {
        if (!item) return UINT32_MAX;
        const auto table = *reinterpret_cast<const uintptr_t**>(item);
        if (!table || !table[0xd8 / 8]) return UINT32_MAX;
        // use the virtual limit that the native inventory writer clamps against.
        const auto value = reinterpret_cast<int(*)(uintptr_t)>(table[0xd8 / 8])(item);
        return value >= 0 ? static_cast<uint32_t>(value) : UINT32_MAX;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return UINT32_MAX; }
}
bool equip_item(uintptr_t p, uintptr_t item) {
    if (!p || !item) return false;
    const auto vtable = *reinterpret_cast<uintptr_t**>(p);
    if (!vtable) return false;
    const auto equip = vtable[0x1588 / 8];
    if (equip != image_base + 0x1432100) return false;
    return reinterpret_cast<char(*)(uintptr_t, uintptr_t)>(equip)(p, item) != 0;
}

char hud_element_setup_detour(uintptr_t element) {
    const char result = original_hud_element_setup ? original_hud_element_setup(element) : 0;
    if (element) {
        __try {
            const auto vtable = *reinterpret_cast<uintptr_t*>(element);
            if (vtable == image_base + rva_mission_challenge_vtable) {
                challenge_player.store(current_hud_player(), std::memory_order_release);
                challenge_element.store(element, std::memory_order_release);
                completion_owner.store(element, std::memory_order_release);
                hud_trace.record(save::BStage::catalog, save::BStatus::entered, "hud_earnings_setup", 0,
                    {{"mission_challenge_owner", element}, {"expected_vtable", image_base + rva_mission_challenge_vtable},
                     {"observed_vtable", vtable}, {"player", challenge_player.load(std::memory_order_acquire)}});
            }
            else {
                challenge_element.store(0, std::memory_order_release);
                challenge_player.store(0, std::memory_order_release);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return result;
}

// a missing inventory item prevents restoring a stale native handle
struct HeldWeapon {
    uintptr_t decl = 0;
    uintptr_t item = 0;
};

HeldWeapon held_weapon_snapshot(uintptr_t p, uintptr_t inv) {
    HeldWeapon result{};
    const auto decl = current_weapon_decl(p);
    if (!decl) return result;
    result.decl = decl;
    result.item = find_item(inv, decl);
    return result;
}
bool ensure_item(uintptr_t inv, uintptr_t p, const char* path, uintptr_t& decl_out, uintptr_t& item_out, bool& mutated) {
    decl_out = find_decl(path);
    if (!decl_out) return false;
    item_out = find_item(inv, decl_out);
    if (!item_out) {
        item_out = give_item(inv, p, decl_out, 1, 0);
        if (item_out) mutated = true;
    }
    return item_out != 0;
}
} // namespace

bool read(void*, uintptr_t p, SnapshotFacts& facts) {
    if (!p) return false;
    __try {
        // if this sample can't read a value, report unknown; don't reuse an older value
        facts = {};
        const auto inv = inventory_of(p);
        if (!inv) return false;

        const auto crucible_decl = find_decl(CRUCIBLE_PATH);
        const auto hammer_decl = find_decl(HAMMER_PATH);
        if (crucible_decl) {
            facts.known |= SC_SPECIAL_KNOWN_CRUCIBLE;
            facts.native_crucible = find_item(inv, crucible_decl) ? 1 : 0;
        }
        if (hammer_decl) {
            facts.known |= SC_SPECIAL_KNOWN_HAMMER;
            facts.native_hammer = find_item(inv, hammer_decl) ? 1 : 0;
        }
        uintptr_t perk_decls[3]{};
        uint8_t effective_perks = 0;
        if (native_upgrade_ready.load(std::memory_order_acquire) && hammer_perks(p, perk_decls, effective_perks)) {
            facts.known |= SC_SPECIAL_KNOWN_HAMMER_PERKS;
            facts.native_hammer_perks = effective_perks;
        }

        facts.held_weapon_decl = current_weapon_decl(p);
        bool route_c = false, route_h = false;
        if (!route_scope_reason(p) && route_ownership(p, route_c, route_h)) {
            publish_policy(p);
            facts.native_selected = static_cast<uint8_t>(applied_mode(route_c, route_h));
            facts.known |= SC_SPECIAL_KNOWN_SELECTION;
            facts.selection_policy = true;
        }

        if (crucible_decl && facts.native_crucible) {
            const auto ammo_decl = find_decl(CRUCIBLE_AMMO_PATH);
            if (ammo_decl) {
                const auto ammo_item = find_item(inv, ammo_decl);
                if (ammo_item) {
                    facts.known |= SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE;
                    facts.crucible_charge = item_count(ammo_item);
                    facts.crucible_charge_max = UINT32_MAX; // natively owned capacity isn't guessed
                }
            }
        }

        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

namespace {
bool activate_hammer_perks(uintptr_t p,const uintptr_t (&perk_decls)[3]) {
    bool requested=false;
    const auto component = p + perk_component_offset;
    const auto active = reinterpret_cast<ActivePerk>(image_base + rva_active_perk);
    const auto unlock = reinterpret_cast<UnlockPerk>(image_base + rva_unlock_perk);
    const auto activate = reinterpret_cast<ActivatePerk>(image_base + rva_activate_perk);
    for (const auto perk_decl : perk_decls) {
        if (active(component, perk_decl)) continue;
        requested = true;
        unlock(component, perk_decl, 0, 0, 0, 0);
        activate(component, perk_decl, 1, 0, 0, 0, 1);
    }
    return requested;
}
bool restore_acquired_weapon(uintptr_t p,const HeldWeapon& before) {
    // acquisition can switch hands; restore the exact prior weapon
    const auto decision = acquisition_restore(before.decl, before.item, current_weapon_decl(p));
    if (decision == AcquisitionRestore::fail_closed) return false;
    if (decision == AcquisitionRestore::equip_prior && !equip_item(p, before.item)) return false;
    if (before.decl && current_weapon_decl(p) != before.decl) return false;
    return true;
}
uint32_t ensure_owned_items(uintptr_t inv,uintptr_t p,uint32_t own_crucible,uint32_t own_hammer,bool& mutated) {
    uintptr_t decl=0,item=0;
    if (own_crucible && !ensure_item(inv,p,CRUCIBLE_PATH,decl,item,mutated)) return 3;
    if (own_hammer && !ensure_item(inv,p,HAMMER_PATH,decl,item,mutated)) return 4;
    return 0;
}
}
uint32_t ensure(void*, uintptr_t p, uint32_t own_crucible, uint32_t own_hammer, uint32_t hammer_tier) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        const auto inv = inventory_of(p);
        if (!inv) return 2;
        const bool upgraded = own_hammer && hammer_tier >= SC_SPECIAL_HAMMER_TIER_UPGRADED;
        uintptr_t perk_decls[3]{};
        uint8_t effective_perks = 0;
        if (upgraded && (!native_upgrade_ready.load(std::memory_order_acquire) || !hammer_perks(p, perk_decls, effective_perks))) return ERROR_NOT_SUPPORTED;
        const auto before = held_weapon_snapshot(p, inv);

        bool mutated = false;
        const auto acquisition=ensure_owned_items(inv,p,own_crucible,own_hammer,mutated);
        if (acquisition) return acquisition;

        bool perk_requested = false;
        if (upgraded) { perk_requested=activate_hammer_perks(p,perk_decls); }

        if ((mutated || perk_requested) && !restore_acquired_weapon(p,before)) return 6;

        SnapshotFacts facts{};
        if (!read(nullptr, p, facts)) return 7;
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}
uint32_t select(void*, uintptr_t p, uint32_t selected) {
    if (!p) return 1;
    if (selected != SC_SPECIAL_WEAPON_CRUCIBLE && selected != SC_SPECIAL_WEAPON_HAMMER) return 2;
    uint32_t error = 0;
    __try {
        if (route_scope_reason(p)) return ERROR_NOT_SUPPORTED;
        bool c = false, h = false;
        if (!route_ownership(p, c, h)) return ERROR_READ_FAULT;
        if (!(selected == SC_SPECIAL_WEAPON_CRUCIBLE ? c : h)) return ERROR_NOT_FOUND;
        if (route_requested == selected) return 0;
        const auto held = current_weapon_decl(p);
        const auto action = *reinterpret_cast<uint32_t*>(p + 0x15f98);
        if (special_down || !held || held == find_decl(CRUCIBLE_PATH) || held == find_decl(HAMMER_PATH) || action > 2) {
            route_trace.record(save::BStage::special_toggle, save::BStatus::refused, "selection_busy", GetTickCount64(),
                {{"requested", selected}, {"applied", applied_mode(c, h)}, {"held_decl", held},
                 {"hands_action", action}, {"special_down", special_down.load()}, {"error", ERROR_BUSY}});
            return ERROR_BUSY;
        }
        const auto before = applied_mode(c, h);
        AcquireSRWLockExclusive(&route_namespace_lock);
        route_requested = selected;
        published_policy = {p, route_epoch.load(), selected, c, h, true};
        std::memcpy(published_policy.namespace_id, route_namespace, 65);
        ReleaseSRWLockExclusive(&route_namespace_lock);
        route_trace.record(save::BStage::special_toggle, save::BStatus::succeeded, "core_policy_applied", GetTickCount64(),
            {{"requested", selected}, {"before", before}, {"applied", applied_mode(c, h)},
             {"held_before", held}, {"held_after", current_weapon_decl(p)}, {"error", 0}});
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}
uint32_t refill_meter_read(uintptr_t meter, size_t slot) noexcept {
    __try {
        const auto table = *reinterpret_cast<const uintptr_t*>(meter);
        const auto value = reinterpret_cast<float(*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(table + slot))(meter);
        return std::isfinite(value) && value >= 0 && static_cast<double>(value) < UINT32_MAX ? static_cast<uint32_t>(value) : UINT32_MAX;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return UINT32_MAX; }
}
uint32_t refill_crucible(uintptr_t p) {
    RefillPool diagnostic{};
    diagnostic.list_source = 2; diagnostic.step = "crucible_inventory";
    refill_name(diagnostic.weapon, CRUCIBLE_PATH); refill_name(diagnostic.declaration, CRUCIBLE_PATH);
    const auto finish = [&](uint32_t error) { diagnostic.error = error; record_refill(diagnostic); return error; };
    if (!p) return finish(ERROR_INVALID_PARAMETER);
    __try {
        const auto inv = inventory_of(p);
        const auto crucible = find_decl(CRUCIBLE_PATH);
        diagnostic.weapon_decl = diagnostic.ammo_decl = crucible;
        if (!inv || !crucible || !find_item(inv, crucible)) return finish(ERROR_NOT_FOUND);
        const auto meter = p + judgement_meter_offset;
        diagnostic.list_decl = meter; diagnostic.step = "crucible_meter";
        const auto table = *reinterpret_cast<uintptr_t*>(meter);
        if (table != image_base + rva_judgement_meter_vtable) return finish(ERROR_NOT_SUPPORTED);
        using MeterRead = float(*)(uintptr_t);
        diagnostic.step = "capacity_before";
        const auto capacity = reinterpret_cast<MeterRead>( *reinterpret_cast<uintptr_t*>(table + 0x28))(meter);
        if (!std::isfinite(capacity) || capacity <= 0) return finish(ERROR_INVALID_DATA);
        diagnostic.capacity_before = refill_meter_read(meter, 0x28);
        diagnostic.quantity_before = refill_meter_read(meter, 0x20);
        diagnostic.step = "meter_writer";
        reinterpret_cast<void(*)(uintptr_t, float)>(image_base + rva_meter_set)(meter, capacity);
        diagnostic.step = "quantity_after";
        const auto amount = reinterpret_cast<MeterRead>( *reinterpret_cast<uintptr_t*>(table + 0x20))(meter);
        diagnostic.quantity_after = refill_meter_read(meter, 0x20);
        diagnostic.capacity_after = refill_meter_read(meter, 0x28);
        diagnostic.step = amount == capacity ? "confirmed" : "quantity_confirmation";
        return finish(amount == capacity ? ERROR_SUCCESS : ERROR_WRITE_FAULT);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return finish(GetExceptionCode()); }
}
const char* refill_pool(const char* weapon) {
    const struct { const char* weapon; const char* pool; } pools[]{
        {"weapon/player/shotgun", "shells"}, {"weapon/player/double_barrel", "shells"},
        {"weapon/player/heavy_cannon", "bullets"}, {"weapon/player/chaingun", "bullets"},
        {"weapon/player/plasma_rifle", "cells"}, {"weapon/player/gauss_rifle", "cells"},
        {"weapon/player/rocket_launcher", "rockets"}, {"weapon/player/chainsaw", "fuel"},
        {"weapon/player/bfg", "bfg"}, {"weapon/player/unmaykr", "bfg"}
    };
    for (const auto& entry : pools) if (!std::strcmp(weapon, entry.weapon)) return entry.pool;
    return nullptr;
}
bool refill_shared_pool(uintptr_t inv, uintptr_t p, RefillPool& diagnostic) {
    const auto ammo_decl = diagnostic.ammo_decl;
    if (!ammo_decl) { diagnostic.step = "pool_missing"; record_refill(diagnostic); return false; }
    diagnostic.step = "quantity_before";
    const auto before_item = find_item(inv, ammo_decl);
    diagnostic.quantity_before = item_count(before_item);
    diagnostic.capacity_before = refill_capacity(before_item);
    diagnostic.step = "writer";
    diagnostic.writer_result = give_item(inv, p, ammo_decl, 999, 1);
    diagnostic.writer_result_known = true;
    diagnostic.step = "quantity_after";
    const auto after_item = find_item(inv, ammo_decl);
    diagnostic.quantity_after = item_count(after_item);
    diagnostic.capacity_after = refill_capacity(after_item);
    const bool full = after_item && diagnostic.capacity_after != UINT32_MAX && diagnostic.quantity_after == diagnostic.capacity_after;
    diagnostic.step = !diagnostic.writer_result ? "writer_refused" : !after_item ? "after_item_missing" : full ? "confirmed" : "quantity_confirmation";
    record_refill(diagnostic);
    return diagnostic.writer_result && full;
}

namespace {
void refill_inventory_weapon(uintptr_t inv,uintptr_t p,uintptr_t item,RefillPool& diagnostic,
                             std::array<const char*,6>& written,uint32_t& attempted,uint32_t& confirmed) {
    if (!item) return;
    const auto decl = *reinterpret_cast<uintptr_t*>(item + 0x38);
    const auto path = decl_path(decl);
    if (!decl || !path) return;
    if (std::strncmp(path, "weapon/player/", 14) != 0) return;
    if (std::strcmp(path, CRUCIBLE_PATH) == 0 || std::strcmp(path, HAMMER_PATH) == 0) return;
    diagnostic.weapon_decl = decl;
    refill_name(diagnostic.weapon, path);

    const auto pool = refill_pool(path);
    if (!pool || std::any_of(written.begin(), written.end(), [&](const char* prior) {
            return prior && !std::strcmp(prior, pool);
        })) return;
    written[attempted++] = pool;
    diagnostic.step = "shared_pool";
    sprintf_s(diagnostic.declaration, "ammo/sharedammopool/%s", pool);
    diagnostic.ammo_decl = find_decl(diagnostic.declaration);
    if (refill_shared_pool(inv, p, diagnostic)) ++confirmed;
}
}
uint32_t refill(void*, uintptr_t p) {
    uint32_t error = 0;
    RefillPool diagnostic{};
    diagnostic.step = "inventory";
    if (!p) { diagnostic.error = 1; record_refill(diagnostic); return 1; }
    __try {
        const auto inv = inventory_of(p);
        if (!inv) { diagnostic.error = 2; record_refill(diagnostic); return 2; }
        const auto count_fn = reinterpret_cast<ItemCount>(image_base + rva_item_count);
        const auto at_fn = reinterpret_cast<ItemAt>(image_base + rva_item_at);

        uint32_t attempted = 0, confirmed = 0;
        std::array<const char*, 6> written{};
        const auto inventory_count = count_fn(inv);
        if (inventory_count > 4096) { diagnostic.step = "inventory_count"; diagnostic.error = 3; record_refill(diagnostic); return 3; }
        // ordinary pools use the inventory writer; crucible charges use their native judgement meter. hammer resources are not ammo pools
        for (int i = 0; i < static_cast<int>(inventory_count); ++i) {
            diagnostic = {}; diagnostic.step = "weapon";
            refill_inventory_weapon(inv,p,at_fn(inv,i),diagnostic,written,attempted,confirmed);
        }
        if (!attempted || confirmed != attempted) return 4;
        const auto crucible = find_decl(CRUCIBLE_PATH);
        if (crucible && find_item(inv, crucible)) { return refill_crucible(p); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); diagnostic.error = error; record_refill(diagnostic); }
    return error;
}

namespace {
RouteObservation observe_route(uintptr_t p) {
    RouteObservation observed{};
    __try {
        SnapshotFacts facts{};
        if (!read(nullptr, p, facts)) return observed;
        observed.held = facts.held_weapon_decl;
        observed.known = facts.known;
        if (facts.known & SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE) observed.charge = facts.crucible_charge;
        observed.action = *reinterpret_cast<uint32_t*>(p + 0x15f98);
        // selectpendingweapon (1461520) writes this; clearpendingweapon (14573c0) writes -1
        observed.pending_quick_slot = *reinterpret_cast<int32_t*>(p + 0x7158);
        const auto resource = p + 0x177a0;
        const auto resource_table = *reinterpret_cast<uintptr_t*>(resource);
        const auto amount = reinterpret_cast<float(*)(uintptr_t)>( *reinterpret_cast<uintptr_t*>(resource_table + 0x20))(resource);
        const auto range = reinterpret_cast<float(*)(uintptr_t)>( *reinterpret_cast<uintptr_t*>(resource_table + 0x28))(resource);
        std::memcpy(&observed.component20_bits, &amount, sizeof(amount));
        std::memcpy(&observed.component28_bits, &range, sizeof(range));
        const auto status = reinterpret_cast<uintptr_t(*)(uintptr_t)>( *reinterpret_cast<uintptr_t*>(*reinterpret_cast<uintptr_t*>(p) + 0x570))(p);
        const auto world = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + 0x699150)( *reinterpret_cast<uintptr_t*>(image_base + 0x45f7370));
        const auto challenge = world ? *reinterpret_cast<uintptr_t*>(world + 0x35f8) : 0;
        const auto effect = status && challenge ? reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(
            image_base + 0x156f4b0)(status, *reinterpret_cast<uintptr_t*>(challenge + 0x148)) : 0;
        if (effect) {
            const auto table = *reinterpret_cast<uintptr_t*>(effect);
            observed.effect68 = reinterpret_cast<bool(*)(uintptr_t)>(*reinterpret_cast<uintptr_t*>(table + 0x68))(effect);
            observed.effect80 = reinterpret_cast<bool(*)(uintptr_t)>(*reinterpret_cast<uintptr_t*>(table + 0x80))(effect);
        }
        const auto component = p + 0x26568;
        if (*reinterpret_cast<uintptr_t*>(component + 0x2b8)) {
            const auto meter = reinterpret_cast<float(*)(uintptr_t)>(image_base + 0x1643260)(component) * 1000;
            if (std::isfinite(meter) && meter >= 0 && static_cast<double>(meter) <= UINT32_MAX) observed.meter_milli = static_cast<uint32_t>(meter);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { observed.error = GetExceptionCode(); }
    return observed;
}
bool ordinary_hands_state(const RouteObservation& observed) {
    return !observed.error && observed.action<=2 && observed.held && observed.held!=find_decl(CRUCIBLE_PATH) && observed.held!=find_decl(HAMMER_PATH);
}
void sample_use_attempt(uintptr_t p, bool safe) {
    const auto started = use_attempt_started.load();
    if (!started) return;
    safe = safe && !route_scope_reason(p);
    const auto observed = safe ? observe_route(p) : RouteObservation{};
    record_use("special_attempt_sample", observed.error ? save::BStatus::refused : save::BStatus::succeeded,
        {{"player", p}, {"attempt_started", started}, {"safe", safe}, {"selected", route_requested.load()},
         {"held_decl", observed.held}, {"hands_action", observed.action}, {"inventory_amount", observed.charge},
         {"resource_known", observed.known}, {"component20_float_bits", observed.component20_bits},
         {"component28_float_bits", observed.component28_bits}, {"effect68", observed.effect68},
         {"effect80", observed.effect80}, {"error", observed.error}, {"pending_quick_slot", observed.pending_quick_slot}});
    const auto elapsed = GetTickCount64() - started;
    const bool stable = !special_down && ordinary_hands_state(observed);
    if (!safe || elapsed >= 10000 || (elapsed >= 500 && stable)) {
        record_use("special_attempt_end", safe && stable ? save::BStatus::succeeded : save::BStatus::refused,
            {{"attempt_started", started}, {"stable", stable}, {"invalidated", !safe}, {"timeout", elapsed >= 10000}});
        AcquireSRWLockExclusive(&use_history_lock);
        if (use_events.attempt_started == started) {
            use_events.attempt_active = false;
            use_attempt_started = 0;
        }
        ReleaseSRWLockExclusive(&use_history_lock);
    }
}
void input_job_detour(uintptr_t context) {
    const auto previous_player = input_job_player;
    const auto previous_epoch = input_job_epoch;
    const auto p = route_player.load(std::memory_order_acquire);
    const auto epoch = route_epoch.load(std::memory_order_acquire);
    // pe 9809708c: 1441f70 submits player+4d230 to 1438f00, which tail-jumps through 1442480 into the ordinary input consumer 1442710
    input_job_player = p && context == p + 0x4d230 ? p : 0;
    input_job_epoch = epoch;
    const auto reason = route_scope_reason(p);
    record_use("input_job_scope", !reason ? save::BStatus::succeeded : save::BStatus::refused,
        {{"entry_rva", 0x1438f00}, {"context", context}, {"player", p}, {"epoch", epoch},
         {"context_match", input_job_player != 0}, {"scope_reason", reason}});
    __try { original_input_job(context); }
    __finally {
        input_job_player = previous_player;
        input_job_epoch = previous_epoch;
    }
}

__declspec(noinline) void weapon_dispatch_detour(uintptr_t p, uintptr_t command, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
    const auto previous_player = input_job_player;
    const auto previous_epoch = input_job_epoch;
    const bool site = caller == image_base + 0x13f225c || caller == image_base + 0x13f203a;
    input_job_player = site ? p : 0;
    input_job_epoch = route_epoch.load(std::memory_order_acquire);
    const auto reason = route_scope_reason(p);
    record_use("weapon_dispatch_scope", !reason ? save::BStatus::succeeded : save::BStatus::refused,
        {{"caller_rva", caller - image_base}, {"player", p}, {"command", command},
         {"caller_match", site}, {"scope_reason", reason}, {"epoch", input_job_epoch}});
    __try { original_weapon_dispatch(p, command); }
    __finally {
        input_job_player = previous_player;
        input_job_epoch = previous_epoch;
    }
}

__declspec(noinline) bool input_pressed_detour(uintptr_t input, uint64_t mask, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
    const bool raw = original_input_pressed(input, mask);
    if (mask == 0x8000000 && raw) {
        static std::atomic<uint64_t> presses{0};
        input_trace.record(save::BStage::chainsaw_input,save::BStatus::entered,"chainsaw_native_input_edge",0,
            {{"edges",++presses},{"caller_rva",caller-image_base},{"input",input},{"raw",raw}});
    }
    if (caller == image_base + 0x1465640 || mask == special_button) {
        if (mask == special_button && raw && !special_down.exchange(true)) begin_use_attempt();
        const auto p = route_player.load(std::memory_order_acquire);
        const auto reason = route_scope_reason(p);
        record_use("crucible_input_edge", reason ? save::BStatus::refused : save::BStatus::succeeded,
            {{"caller_rva", caller - image_base}, {"input", input}, {"mask", mask}, {"raw_al", raw},
             {"scope_reason", reason}, {"policy_requested", route_requested.load()}, {"result_al", raw}});
    }
    return raw;
}

__declspec(noinline) bool crucible_activate_detour(uintptr_t p, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
    const auto reason = route_scope_reason(p);
    const auto before = !reason ? observe_route(p) : RouteObservation{};
    record_use("crucible_activate_enter", reason ? save::BStatus::refused : save::BStatus::entered,
        {{"caller_rva", caller - image_base}, {"player", p}, {"scope_reason", reason},
         {"inventory_amount", before.charge}, {"known", before.known}, {"error", before.error},
         {"component20_float_bits", before.component20_bits}, {"component28_float_bits", before.component28_bits},
         {"effect68", before.effect68}, {"effect80", before.effect80}, {"hands_action", before.action}, {"held_decl", before.held},
         {"pending_quick_slot", before.pending_quick_slot}});
    const bool result = original_crucible_activate(p);
    const bool observed = !reason && !route_scope_reason(p);
    const auto after = observed ? observe_route(p) : RouteObservation{};
    record_use("crucible_activate_return", result ? save::BStatus::succeeded : save::BStatus::refused,
        {{"caller_rva", caller - image_base}, {"player", p}, {"native_result_al", result},
         {"scope_valid", observed}, {"charge_before", before.charge}, {"charge_after", after.charge},
         {"known_before", before.known}, {"known_after", after.known}, {"error", after.error},
         {"component20_float_bits", after.component20_bits}, {"component28_float_bits", after.component28_bits},
         {"effect68", after.effect68}, {"effect80", after.effect80}, {"hands_action", after.action}, {"held_decl", after.held},
         {"pending_quick_slot", after.pending_quick_slot}});
    return result;
}

__declspec(noinline) void hammer_attack_detour(uintptr_t p, void* hook_caller = nullptr) {
    const auto caller = reinterpret_cast<uintptr_t>(hook_caller);
    const auto reason = route_scope_reason(p);
    const auto before = !reason ? observe_route(p) : RouteObservation{};
    record_use("hammer_handler_enter", reason ? save::BStatus::refused : save::BStatus::entered,
        {{"caller_rva", caller - image_base}, {"player", p}, {"scope_reason", reason},
         {"hands_action", before.action}, {"meter_milli", before.meter_milli}, {"error", before.error}});
    original_hammer_attack(p);
    const bool observed = !reason && !route_scope_reason(p);
    const auto after = observed ? observe_route(p) : RouteObservation{};
    record_use("hammer_handler_return", observed ? save::BStatus::succeeded : save::BStatus::refused,
        {{"caller_rva", caller - image_base}, {"player", p}, {"scope_valid", observed},
         {"action_observed", observed && !after.error && after.action != UINT32_MAX},
         {"action_before", before.action}, {"action_after", after.action},
         {"action_started", observed && !before.error && !after.error && before.action != 0x21 && after.action == 0x21},
         {"meter_before", before.meter_milli}, {"meter_after", after.meter_milli}, {"error", after.error}});
}
}
bool present(void*, uintptr_t p, uint32_t, uint32_t, uint32_t) {
    // weaponinfo update owns the swf tree; a publish is visible there on its next callback
    const auto epoch = route_epoch.load(std::memory_order_acquire);
    if (p && p == route_player.load(std::memory_order_acquire) && epoch == native::observation_stamp()) {
        static thread_local uint64_t last_revision = UINT64_MAX, last_epoch = 0;
        static thread_local uintptr_t last_player = 0;
        char namespace_id[65];
        AcquireSRWLockShared(&route_namespace_lock);
        std::memcpy(namespace_id, route_namespace, sizeof(namespace_id));
        ReleaseSRWLockShared(&route_namespace_lock);
        const auto owner = hud_owner_snapshot(namespace_id);
        if (owner.revision != last_revision || epoch != last_epoch || p != last_player) {
            last_revision = owner.revision;
            last_epoch = epoch;
            last_player = p;
            hud_trace.record(save::BStage::profile_publish, save::BStatus::pending, "hud_presentation_request", 0,
                {{"weapon_info_owner", weapon_info_element.load(std::memory_order_acquire)},
                 {"mission_challenge_owner", challenge_element.load(std::memory_order_acquire)},
                 {"player", p}, {"observation_epoch", epoch},
                 {"lifecycle_generation", native::inspect(0).scope.lifecycle_generation},
                 {"graphics_ready", hud::graphics_ready}, {"keycap_ready", hud::keycap_ready},
                 {"balance_known", owner.refill_balance <= 3}, {"balance", owner.refill_balance},
                 {"revision", owner.revision}, {"keys", configured_keys.load(std::memory_order_relaxed)}});
        }
    }
    return false;
}
bool present_challenge_completion(uintptr_t widget,const char* name, const char* reward) {
    const auto element=completion_owner.load(std::memory_order_acquire);
    if (!element || !widget) return false;
    __try {
        if (*reinterpret_cast<uintptr_t*>(element)!=image_base+rva_mission_challenge_vtable) return false;
        bool owned=false;
        for (size_t i=0;i<3;++i) owned |= *reinterpret_cast<uintptr_t*>(element+0x108+i*sizeof(uintptr_t))==widget;
        if (!owned) return false;
        auto append=reinterpret_cast<EarningsAppend>(image_base+rva_hud_earnings);
#ifdef SC_NATIVE_TESTING
        if (test_earnings_append) append=test_earnings_append;
#endif
        append(element,name,reward,5000,0,0);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void selection_toggle_label(const SnapshotFacts& facts,char (&key)[32]) {
    const auto vk = static_cast<int>((configured_keys.load(std::memory_order_relaxed) >> 8) & 0xff);
    if (facts.native_crucible && facts.native_hammer && vk) {
        if (vk >= VK_F1 && vk <= VK_F12) std::snprintf(key, sizeof(key), player_strings::hud_function_key_toggle, vk - VK_F1 + 1);
        else {
            char label[16]{};
            if (GetKeyNameTextA(static_cast<LONG>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16), label, sizeof(label)))
                std::snprintf(key, sizeof(key), player_strings::hud_key_toggle, label);
        }
    }
}
bool selection_facts(uintptr_t p,SnapshotFacts& facts) {
#ifdef SC_NATIVE_TESTING
    return test_selection_read ? test_selection_read(p,facts) : read(nullptr,p,facts);
#else
    return read(nullptr,p,facts);
#endif
}
void present_selection(uintptr_t p) {
    SnapshotFacts facts{};
    const bool observed=selection_facts(p,facts);
    if (!observed || !facts.selection_policy || !facts.native_selected) return;
    const auto element = challenge_element.load(std::memory_order_acquire);
    if (!element || challenge_player.load(std::memory_order_acquire) != p) return;
    char text[96]{}, resource[32]{}, key[32]{};
    strcpy_s(resource, player_strings::hud_resource_unknown);
    selection_toggle_label(facts,key);
    if (facts.native_selected == SC_SPECIAL_WEAPON_CRUCIBLE && (facts.known & SC_SPECIAL_KNOWN_CRUCIBLE_RESOURCE))
        std::snprintf(resource, sizeof(resource), player_strings::hud_charges, facts.crucible_charge);
    __try {
        if (*reinterpret_cast<uintptr_t*>(element) != image_base + rva_mission_challenge_vtable) return;
        if (facts.native_selected == SC_SPECIAL_WEAPON_HAMMER && *reinterpret_cast<uintptr_t*>(p + equipment_upgrade_offset + 0x2b8)) {
            const auto meter = reinterpret_cast<float(*)(uintptr_t)>(image_base + 0x1643260)(p + equipment_upgrade_offset);
            if (std::isfinite(meter) && meter >= 0) std::snprintf(resource, sizeof(resource), player_strings::hud_meter, static_cast<double>(meter * 100));
        }
        std::snprintf(text, sizeof(text), "%s%s", resource, key);
        auto append = reinterpret_cast<EarningsAppend>(image_base + rva_hud_earnings);
#ifdef SC_NATIVE_TESTING
        if (test_earnings_append) append = test_earnings_append;
#endif
        append(element, facts.native_selected == SC_SPECIAL_WEAPON_CRUCIBLE ? player_strings::hud_crucible_selected :
                player_strings::hud_hammer_selected, text, 3000, 0, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
Calls calls{nullptr, player, read, ensure, select, refill, present};

namespace {
std::atomic<uint64_t> key_state_checked{0};
struct KeyLatch {
    bool down = false, latched = true;
    uint64_t released_at = 0;
    int key = -1;

    bool press(int vk, bool pressed, uint64_t now, bool enabled) {
        if (key != vk || !enabled) {
            key = vk; latched = true; released_at = 0; down = pressed;
            return false;
        }
        if (!vk) { down = false; return false; }
        if (latched) {
            down = pressed;
            if (pressed) released_at = 0;
            else if (!released_at) released_at = now;
            else if (now - released_at >= 120) { latched = false; released_at = 0; }
            return false;
        }
        const bool rising = pressed && !down;
        down = pressed;
        if (rising) { latched = true; released_at = 0; }
        return rising;
    }
};
KeyLatch input_latches[2];
} // namespace

void refresh_input_config() {
    const auto now = GetTickCount64();
    if (now - key_state_checked.load(std::memory_order_relaxed) < 250) return;
    key_state_checked.store(now, std::memory_order_relaxed);
    wchar_t absolute[MAX_PATH]{};
    const auto length = GetFullPathNameW(L"base\\ap_queue", MAX_PATH, absolute, nullptr);
    const bool path_valid = length && length < MAX_PATH;
    AcquireSRWLockExclusive(&directory_lock);
    controls_directory = path_valid ? absolute : L"";
    ReleaseSRWLockExclusive(&directory_lock);
    const auto keys = path_valid ? controls::read(absolute) : controls::Bindings{};
    // a shared key can't dispatch two actions; conflicting files disable both
    const unsigned packed = !path_valid || keys.conflict() ? 0u : static_cast<unsigned>(keys.keys[0] | (keys.keys[1] << 8));
    configured_keys.store(packed, std::memory_order_relaxed);
    input_trace.record(save::BStage::profile_read, save::BStatus::succeeded, "control_bindings", 0,
        {{"path_valid", path_valid}, {"refill_vk", keys.keys[0]}, {"toggle_vk", keys.keys[1]},
         {"invalid", keys.invalid}, {"conflict", keys.conflict()}});
}
void refresh_input_route(uintptr_t p,bool safe_gameplay) {
    if (!safe_gameplay || !p) {
        challenge_element.store(0, std::memory_order_release);
        challenge_player.store(0, std::memory_order_release);
        weapon_info_element.store(0, std::memory_order_release);
        weapon_info_player.store(0, std::memory_order_release);
        weapon_info_epoch.store(0, std::memory_order_release);
        route_epoch.store(0, std::memory_order_release);
        route_player.store(0, std::memory_order_release);
    }
    AcquireSRWLockExclusive(&route_namespace_lock);
    if (!safe_gameplay || !p || published_policy.player != p || published_policy.epoch != native::observation_stamp()) published_policy = {};
    ReleaseSRWLockExclusive(&route_namespace_lock);
    route_thread = GetCurrentThreadId();
    if (p && safe_gameplay) {
        const auto& id = save::session().namespace_id();
        AcquireSRWLockExclusive(&route_namespace_lock);
        if (std::memcmp(route_namespace, id.c_str(), 65)) {
            published_policy = {};
            std::memcpy(route_namespace, id.c_str(), 65);
            route_requested = SC_SPECIAL_WEAPON_NONE;
            special_down = false;
        }
        ReleaseSRWLockExclusive(&route_namespace_lock);
        route_player.store(p, std::memory_order_release);
        route_epoch.store(native::observation_stamp(), std::memory_order_release);
        publish_policy(p);
    }
}
const char* local_toggle_predicate(const sc_special_result& result,bool changed,bool unchanged) {
    if (changed) return "local_toggle_selected";
    if (unchanged) return "local_toggle_already_selected";
    if (result.kind==SC_SPECIAL_SELECT) return "local_toggle_select_failed";
    if (result.outcome!=SC_SPECIAL_OUTCOME_OK) return "local_toggle_observe_failed";
    const auto known=SC_SPECIAL_KNOWN_CRUCIBLE|SC_SPECIAL_KNOWN_HAMMER;
    if ((result.native_state_known&known)!=known) return "local_toggle_observe_incomplete";
    return "local_toggle_no_owned_weapon";
}
void dispatch_local_toggle(uintptr_t p,uint64_t now) {
    const auto result = toggle_local(save::session().namespace_id().c_str(), calls);
    const bool changed = result.kind == SC_SPECIAL_SELECT && result.outcome == SC_SPECIAL_OUTCOME_OK;
    const bool unchanged = result.kind == SC_SPECIAL_SELECT && result.outcome == SC_SPECIAL_OUTCOME_NOOP &&
        (result.native_state_known & SC_SPECIAL_KNOWN_SELECTION) && result.native_selected == result.selected;
    if (changed) present_selection(p);
    const auto predicate=local_toggle_predicate(result,changed,unchanged);
    save::session().btrace.record(save::BStage::special_toggle, changed || unchanged ? save::BStatus::succeeded : save::BStatus::refused, predicate, now,
        {{"kind", result.kind}, {"outcome", result.outcome},
         {"native_known", result.native_state_known},
         {"native_crucible", result.native_crucible},
         {"native_hammer", result.native_hammer},
         {"selected", result.selected}, {"native_selected", result.native_selected},
         {"native_error", result.native_exception}});
}
save::BStatus toggle_latch_status(bool attempted,bool enabled,bool pressed) {
    if (attempted && !enabled) return save::BStatus::refused;
    return pressed ? save::BStatus::entered : save::BStatus::pending;
}
void poll_toggle_key(uintptr_t p,bool safe_gameplay,bool enabled,bool selection_route,DWORD foreground_pid,uint64_t now,unsigned keys) {
    static bool observed_down=false;
    static uint32_t attempts=0, dispatches=0, gate_refusals=0, admission_refusals=0;
    const auto count = [](uint32_t& value) { if (value != UINT32_MAX) ++value; };
    // the packaged client owns refill requests and their ap ledger transaction
    constexpr unsigned i=1;
    const auto vk = static_cast<int>((keys >> (i * 8)) & 0xff);
    const bool down = vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool attempted = down && !observed_down;
    const bool action_enabled = enabled && selection_route;
    observed_down = down;
    if (attempted) { count(attempts); if (!action_enabled) count(gate_refusals); }
    const bool pressed = input_latches[i].press(vk, down, now, action_enabled);
    if (pressed) count(dispatches);
    input_trace.record(save::BStage::special_toggle, toggle_latch_status(attempted,action_enabled,pressed), "input_latch", 0,
        {{"vk", vk}, {"down", down}, {"enabled", action_enabled}, {"route_ready", selection_route},
         {"latched", input_latches[i].latched},
         {"dispatched", pressed}, {"attempts", attempts}, {"dispatches", dispatches},
         {"gate_refusals", gate_refusals}, {"admission_refusals", admission_refusals}});
    if (!pressed) return;
    // native reconstruction callbacks may invalidate the outer tick scope
    const bool admitted = native::gameplay_admitted();
    if (!admitted) count(admission_refusals);
    save::session().btrace.record(save::BStage::special_input, admitted ? save::BStatus::entered : save::BStatus::refused,
        admitted ? "local_control_admitted" : "local_control_not_admitted", now,
        {{"action", i}, {"vk", vk}, {"safe_gameplay", safe_gameplay},
         {"player", p != 0}, {"foreground", foreground_pid == GetCurrentProcessId()},
         {"gameplay_admitted", admitted}});
    if (!admitted) return;
    dispatch_local_toggle(p,now);
}
void poll_input(uintptr_t p, bool safe_gameplay) {
    sample_use_attempt(p, safe_gameplay);
    refresh_input_route(p,safe_gameplay);
    const bool installed = ready.load(std::memory_order_acquire);
    const bool selection_route = route_ready.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    const auto keys = configured_keys.load(std::memory_order_relaxed);
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
    const bool enabled = installed && safe_gameplay && p && foreground_pid == GetCurrentProcessId() && !ap_controls::input_inhibited();
    input_trace.record(save::BStage::special_input, enabled ? save::BStatus::succeeded : save::BStatus::pending,
        "input_gate", 0, {{"ready", installed}, {"safe_gameplay", safe_gameplay}, {"player", p != 0},
                         {"foreground", foreground_pid == GetCurrentProcessId()}, {"keys", keys}});
    poll_toggle_key(p,safe_gameplay,enabled,selection_route,foreground_pid,now,keys);
}
bool available() { return ready.load(std::memory_order_acquire); }
bool selection_route_available() { return route_ready.load(std::memory_order_acquire); }
save::BSnapshot installation_diagnostics() { return installation_trace.snapshot(); }
save::BSnapshot input_diagnostics() { return input_trace.snapshot(); }
save::BSnapshot route_diagnostics() { return route_trace.snapshot(); }
save::BSnapshot hud_diagnostics() { return hud_trace.snapshot(); }
void present_combat_progress(uintptr_t root, uintptr_t anchor, bool visible) {
    combat::ProgressView progress{};
    const auto& namespace_id = save::session().namespace_id();
    if (visible && namespace_id.size() == 64) progress = combat::presentation().view(namespace_id.c_str());
    hud::project_progress(root, anchor, progress);
}
RefillHistory refill_history() {
    AcquireSRWLockShared(&refill_lock);
    auto snapshot = refill_events;
    ReleaseSRWLockShared(&refill_lock);
    return snapshot;
}
UseHistory use_history() {
    AcquireSRWLockShared(&use_history_lock);
    auto snapshot = use_events;
    ReleaseSRWLockShared(&use_history_lock);
    snapshot.lock_dropped = use_lock_dropped.load(std::memory_order_relaxed);
    return snapshot;
}
std::wstring input_directory() {
    AcquireSRWLockShared(&directory_lock);
    auto result = controls_directory;
    ReleaseSRWLockShared(&directory_lock);
    return result;
}
bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}
void execute_native(const sc_special_request& request, sc_special_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_SPECIAL_OUTCOME_UNAVAILABLE; return; }
    out.flags |= SC_SPECIAL_FLAG_SHARED_STATE_BOUND;
    execute(request, out, calls);
}

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls value, const char* id) {
    calls = value;
    std::memcpy(fixture_namespace, id, 65);
    ready.store(true, std::memory_order_release);

}
#endif

bool crucible_resolver_detour_entry(uintptr_t p) {
    const auto caller = _ReturnAddress();
    const auto lease = selection_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_crucible_resolver)>(lease.original)(p);
        return crucible_resolver_detour(p, caller);
    } __finally { selection_hooks.leave(lease); }
}
bool input_down_detour_entry(uintptr_t input, uint64_t button) {
    const auto caller = _ReturnAddress();
    const auto lease = selection_hooks.enter(1);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_input_down)>(lease.original)(input, button);
        return input_down_detour(input, button, caller);
    } __finally { selection_hooks.leave(lease); }
}
void input_job_detour_entry(uintptr_t context) {
    const auto lease = selection_hooks.enter(2);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_input_job)>(lease.original)(context);
        return input_job_detour(context);
    } __finally { selection_hooks.leave(lease); }
}
void hammer_attack_detour_entry(uintptr_t p) {
    const auto caller = _ReturnAddress();
    const auto lease = selection_hooks.enter(3);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_hammer_attack)>(lease.original)(p);
        return hammer_attack_detour(p, caller);
    } __finally { selection_hooks.leave(lease); }
}
void weapon_dispatch_detour_entry(uintptr_t p, uintptr_t command) {
    const auto caller = _ReturnAddress();
    const auto lease = selection_hooks.enter(4);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_weapon_dispatch)>(lease.original)(p, command);
        return weapon_dispatch_detour(p, command, caller);
    } __finally { selection_hooks.leave(lease); }
}
bool crucible_activate_detour_entry(uintptr_t p) {
    const auto caller = _ReturnAddress();
    const auto lease = selection_hooks.enter(5);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_crucible_activate)>(lease.original)(p);
        return crucible_activate_detour(p, caller);
    } __finally { selection_hooks.leave(lease); }
}
bool input_pressed_detour_entry(uintptr_t input, uint64_t mask) {
    const auto caller = _ReturnAddress();
    const auto lease = selection_hooks.enter(6);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_input_pressed)>(lease.original)(input, mask);
        return input_pressed_detour(input, mask, caller);
    } __finally { selection_hooks.leave(lease); }
}
uint64_t loot_spawn_amount_detour_entry(uintptr_t loot, uintptr_t declaration, uintptr_t player, uintptr_t target, float* amount, float* scale,
                                  uint32_t arg7, uint32_t trigger, uint32_t mode) {
    const auto lease = loot_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_loot_spawn_amount)>(lease.original)(loot, declaration, player, target, amount, scale, arg7, trigger, mode);
        return loot_spawn_amount_detour(loot, declaration, player, target, amount, scale, arg7, trigger, mode);
    } __finally { loot_hooks.leave(lease); }
}

char hud_element_setup_detour_entry(uintptr_t element) {
    const auto lease = toast_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_hud_element_setup)>(lease.original)(element);
        return hud_element_setup_detour(element);
    } __finally { toast_hooks.leave(lease); }
}
void fast_travel_checkpoint_render_detour_entry(uintptr_t screen, int index) {
    const auto lease = fast_travel_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_fast_travel_checkpoint_render)>(lease.original)(screen, index);
        return fast_travel_checkpoint_render_detour(screen, index);
    } __finally { fast_travel_hooks.leave(lease); }
}
uint64_t weapon_info_label_detour_entry(uintptr_t parent, const char* label, bool warn) {
    const auto caller = _ReturnAddress();
    const auto lease = hud_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_weapon_info_label)>(lease.original)(parent, label, warn);
        return weapon_info_label_detour(parent, label, warn, caller);
    } __finally { hud_hooks.leave(lease); }
}
void weapon_hud_update_detour_entry(uintptr_t element, uintptr_t time) {
    const auto lease = hud_hooks.enter(1);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_weapon_hud_update)>(lease.original)(element, time);
        return weapon_hud_update_detour(element, time);
    } __finally { hud_hooks.leave(lease); }
}
void chainsaw_tick_detour_entry(uintptr_t p) {
    const auto lease = chainsaw_hooks.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_chainsaw_tick)>(lease.original)(p);
        return chainsaw_tick_detour(p);
    } __finally { chainsaw_hooks.leave(lease); }
}
uint64_t weapon_info_label_original(uintptr_t parent, const char* label, bool warn) {
    const auto lease = hud_hooks.enter(0);
    __try { return reinterpret_cast<WeaponInfoLabel>(lease.original)(parent, label, warn); }
    __finally { hud_hooks.leave(lease); }
}

namespace {
bool install_required_sites(const engine::Binding& binding, engine::LocalMemory& memory) {
    const auto& sites = builds::steam_20260818().special_required;
#ifdef SC_NATIVE_TESTING
    if (testing::real_hook_targets())
#endif
    for (const auto& s : sites) {
        const auto target = native::target_from_hex(image_base + s.offset, s.bytes);
        std::array<uint8_t, 32> actual{};
        const bool section = binding.image.contains(s.offset, actual.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0);
        const auto read = memory.copy(target.address, actual.data(), actual.size());
        uint64_t expected_words[4]{}, actual_words[4]{};
        std::memcpy(expected_words, target.bytes.data(), 32);
        std::memcpy(actual_words, actual.data(), 32);
        bool valid = section && !read.reason && actual == target.bytes;
        installation_trace.record(save::BStage::native_start, valid ? save::BStatus::entered : save::BStatus::refused,
            valid ? "site_validated" : "site_refused", 0,
            {{"rva", s.offset}, {"section", section}, {"read_reason", read.reason}, {"read_error", read.error},
             {"expected0", expected_words[0]}, {"expected1", expected_words[1]},
             {"expected2", expected_words[2]}, {"expected3", expected_words[3]},
             {"observed0", actual_words[0]}, {"observed1", actual_words[1]},
             {"observed2", actual_words[2]}, {"observed3", actual_words[3]}});
        if (!valid) return false;
    }

    return true;
}
bool install_upgrade_vtable(const engine::Binding& binding, engine::LocalMemory& memory) {
    constexpr uint32_t expected_vtable_slots[] = {0x1641b80, 0x16466a0, 0x1647be0, 0x355140};
    std::array<uintptr_t, std::size(expected_vtable_slots)> actual_vtable{};
#ifdef SC_NATIVE_TESTING
    if (testing::real_hook_targets()) {
#endif
    if (!binding.image.contains(rva_equipment_upgrade_vtable, sizeof(actual_vtable), IMAGE_SCN_MEM_READ, 0) ||
        memory.copy(image_base + rva_equipment_upgrade_vtable, actual_vtable.data(), sizeof(actual_vtable)).reason)
    {
        installation_trace.record(save::BStage::root_layout, save::BStatus::refused, "vtable_unreadable", 0,
                                  {{"rva", rva_equipment_upgrade_vtable}});
        return false;
    }
    for (size_t i = 0; i < actual_vtable.size(); ++i) if (actual_vtable[i] != image_base + expected_vtable_slots[i]) {
            installation_trace.record(save::BStage::root_layout, save::BStatus::refused, "vtable_mismatch", 0,
                {{"rva", rva_equipment_upgrade_vtable}, {"slot", i}, {"expected", image_base + expected_vtable_slots[i]},
                 {"observed", actual_vtable[i]}});
            return false;
        }

#ifdef SC_NATIVE_TESTING
    }
#endif
    return true;
}
struct RouteValidation {
    const engine::Binding& binding;
    engine::Memory& memory;
    const builds::RouteSite& site;
    native::Target target;
    native::ValidationDetail detail{};
    uint32_t owner=0,error=SC_NATIVE_NONE,observed_call=0;
    const char* predicate="route_site_qualified";

    RouteValidation(const engine::Binding& b,engine::Memory& m,const builds::RouteSite& s)
        : binding(b),memory(m),site(s),target(native::target_from_hex(image_base+s.offset,s.bytes)) {
        DWORD64 unwind_base=0;
        const auto unwind=RtlLookupFunctionEntry(target.address,&unwind_base,nullptr);
        owner=unwind && unwind_base==image_base ? unwind->BeginAddress : 0;
    }
    void entry(HANDLE stop,uint64_t deadline) {
        error=native::validate_target(memory,binding.image,target,stop,deadline,&detail);
        if (error==SC_NATIVE_TARGET_BYTES) predicate="route_entry_bytes";
        else if (error==SC_NATIVE_TARGET_BOUNDARY) predicate="route_entry_boundary";
        else if (error==SC_NATIVE_TARGET_NOT_UNIQUE) predicate="route_entry_not_unique";
        else if (error) predicate="route_entry_read_or_budget";
    }
    void leaf(HANDLE stop,uint64_t deadline) {
        error=native::validate_leaf_target(memory,binding.image,target,site.length,stop,deadline,&detail);
        if (error==SC_NATIVE_TARGET_BYTES) predicate="route_leaf_bytes";
        else if (error==SC_NATIVE_TARGET_BOUNDARY) predicate=owner ? "route_leaf_unwind_owner" : "route_leaf_boundary";
        else if (error==SC_NATIVE_TARGET_NOT_UNIQUE) predicate="route_leaf_not_unique";
        else if (error) predicate="route_leaf_read_or_budget";
    }
    void call_instruction(const std::array<uint8_t,32>& actual) {
        size_t offset=0,last=0;
        while (offset<site.length) {
            last=offset;
            hde64s instruction{};
            const auto size=hde64_disasm(actual.data()+offset,&instruction);
            if (!size || instruction.flags&F_ERROR || offset+size>site.length) break;
            offset+=size;
        }
        if (offset!=site.length || last+5!=site.length || actual[last]!=0xe8) {
            error=SC_NATIVE_TARGET_BOUNDARY; predicate="route_window_instructions";
            return;
        }
        if (site.offset+site.length!=rva_hammer_input_return) {
            error=SC_NATIVE_TARGET_BOUNDARY; predicate="route_window_return";
            return;
        }
        int32_t relative=0; std::memcpy(&relative,actual.data()+last+1,sizeof(relative));
        observed_call=static_cast<uint32_t>(site.offset+site.length+relative);
        if (observed_call!=rva_input_down) { error=SC_NATIVE_TARGET_BOUNDARY; predicate="route_window_call_target"; } else if (actual!=target.bytes) {
            error=SC_NATIVE_TARGET_BYTES; predicate="route_window_bytes";
        }
    }
    void window() {
        std::array<uint8_t,32> actual{};
        const bool section=binding.image.contains(site.offset,actual.size(), IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ,IMAGE_SCN_MEM_WRITE);
        engine::ReadResult read{};
        if (section) read=memory.copy(target.address,actual.data(),actual.size());
        else { read.reason=SC_REASON_READ_FAILED; read.error=ERROR_NOACCESS; }
        detail.read_attempted=section; detail.read=read; detail.byte_count=32;
        detail.expected=target.bytes; detail.actual=actual;
        if (!section || read.reason) { error=SC_NATIVE_READ_FAILED; predicate="route_window_read"; return; }
        if (owner!=site.owner || !native::function_window(memory,binding.image, image_base+site.owner,target.address,site.length,&detail)) {
            error=SC_NATIVE_TARGET_BOUNDARY; predicate="route_window_owner"; return;
        }
        call_instruction(actual);
    }
    void validate(HANDLE stop,uint64_t deadline) {
        if (site.kind==builds::RouteSiteKind::function_entry) entry(stop,deadline);
        else if (site.kind==builds::RouteSiteKind::exact_leaf) leaf(stop,deadline);
        else window();
    }
    void record(size_t index) {
    uint64_t expected0 = 0, actual0 = 0;
    std::memcpy(&expected0, detail.expected.data(), sizeof(expected0));
    std::memcpy(&actual0, detail.actual.data(), sizeof(actual0));
    installation_trace.record(save::BStage::special_toggle, error ? save::BStatus::refused : save::BStatus::entered, predicate, 0,
        {{"rva", site.offset}, {"site", index}, {"error", error},
         {"kind", static_cast<uint32_t>(site.kind)}, {"owner_rva", owner}, {"expected_owner", site.owner},
         {"read_attempted", detail.read_attempted}, {"read_reason", detail.read.reason},
         {"read_error", detail.read.error}, {"window_offset", detail.window_offset},
         {"byte_count", detail.byte_count}, {"collision_rva", detail.collision_rva},
         {"expected0", expected0}, {"actual0", actual0},
         {"expected_call", rva_input_down}, {"observed_call", observed_call}});
    }
};
void install_selection_routes(const engine::Binding& binding, engine::LocalMemory& memory, HANDLE stop) {
    const auto& route_sites = builds::steam_20260818().special_routes;
    bool route_valid = true;
    const auto route_deadline = GetTickCount64() + 10000;
    size_t site_index = 0;
#ifdef SC_NATIVE_TESTING
    if (testing::real_hook_targets())
#endif
    for (const auto& site : route_sites) {
        RouteValidation validation(binding,memory,site);
        validation.validate(stop,route_deadline);
        validation.record(site_index);
        if (validation.error) { route_valid = false; break; }
        ++site_index;
    }
    if (route_valid) {
        const hooks::HookSpec specs[]{
            hooks::bind(image_base + rva_crucible_resolver, crucible_resolver_detour_entry, original_crucible_resolver),
            hooks::bind(image_base + rva_input_down, input_down_detour_entry, original_input_down),
            hooks::bind(image_base + 0x1438f00, input_job_detour_entry, original_input_job),
            hooks::bind(image_base + 0x143c7f0, hammer_attack_detour_entry, original_hammer_attack),
            hooks::bind(image_base + 0x14644e0, weapon_dispatch_detour_entry, original_weapon_dispatch),
            hooks::bind(image_base + 0x1456310, crucible_activate_detour_entry, original_crucible_activate),
            hooks::bind(image_base + 0x146be40, input_pressed_detour_entry, original_input_pressed)};
        const bool installed = selection_hooks.install(specs, stop);
        route_ready.store(installed, std::memory_order_release);
        installation_trace.record(save::BStage::special_toggle, installed ? save::BStatus::succeeded : save::BStatus::refused,
            "joint_route_install", 0, {{"ready", installed}});
    }

}
void install_upgrade_sites(const engine::Binding& binding, engine::LocalMemory& memory) {
    // upgrade writers and reader are checked together; base special routing stays independent
    const auto& upgrade_sites = builds::steam_20260818().special_upgrade;
    bool upgrade_valid = true;
#ifdef SC_NATIVE_TESTING
    if (testing::real_hook_targets())
#endif
    for (const auto& site : upgrade_sites) {
        const auto target = native::target_from_hex(image_base + site.offset, site.bytes);
        std::array<uint8_t, 32> actual{};
        const bool section = binding.image.contains(site.offset, actual.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0);
        const auto sample = section ? memory.copy(target.address, actual.data(), actual.size()) : engine::ReadResult{SC_REASON_READ_FAILED, ERROR_NOACCESS};
        const bool valid = section && !sample.reason && actual == target.bytes;
        installation_trace.record(save::BStage::special_toggle, valid ? save::BStatus::entered : save::BStatus::refused,
            valid ? "hammer_upgrade_site_validated" : "hammer_upgrade_site_refused", 0,
            {{"rva", site.offset}, {"section", section}, {"read_reason", sample.reason},
             {"read_error", sample.error}, {"bytes_match", actual == target.bytes}});
        upgrade_valid &= valid;
    }
    native_upgrade_ready.store(upgrade_valid, std::memory_order_release);

}
void install_loot_observation(const engine::Binding& binding, engine::LocalMemory& memory, HANDLE stop) {
    // optional terminal observation of the three hammer loot drop types
    const char* loot_bytes = "488954241048894c24085556574881ecf0000000498bf9498bf0488bea4885d2";
    const auto loot_target = native::target_from_hex(image_base + rva_loot_spawn_amount, loot_bytes);
    const auto loot_validation = native::validate_target(memory, binding.image, loot_target, stop, GetTickCount64() + 10000);
    const hooks::HookSpec loot_specs[]{hooks::bind(loot_target.address, loot_spawn_amount_detour_entry, original_loot_spawn_amount)};
    const bool loot_installed = !loot_validation && loot_hooks.install(loot_specs, stop);
    installation_trace.record(save::BStage::special_toggle,
        loot_installed ? save::BStatus::succeeded : save::BStatus::refused, "hammer_loot_terminal_install", 0,
        {{"rva", rva_loot_spawn_amount}, {"validation", loot_validation}, {"installed", loot_installed}});

}
bool install_hud_capture(const engine::Binding& binding, engine::LocalMemory& memory, HANDLE stop, uint64_t deadline) {
    // optional missionchallenge toast capture is independent of gameplay hooks
    const char* hud_bytes = "4057b860400000e854119801482be0488b05823d2c034833c448898424404000";
    const auto hud_target = native::target_from_hex(image_base + rva_hud_element_setup, hud_bytes);
    const auto hud_validation = native::validate_target(memory, binding.image, hud_target, stop, deadline);
    const hooks::HookSpec toast_specs[]{hooks::bind(hud_target.address, hud_element_setup_detour_entry, original_hud_element_setup)};
    const bool hud_installed = !hud_validation && toast_hooks.install(toast_specs, stop);
    installation_trace.record(save::BStage::profile_publish, hud_installed ? save::BStatus::succeeded : save::BStatus::refused, "optional_hud_hook", 0,
        {{"rva", rva_hud_element_setup}, {"validation", hud_validation}, {"installed", hud_installed}});
    return hud_installed;
}
void install_fast_travel_guard(const engine::Binding& binding, engine::LocalMemory& memory, HANDLE stop, uint64_t deadline) {
    const auto& fast_travel_sites = builds::steam_20260818().fast_travel;
    bool fast_travel_valid = true;
    for (const auto& site : fast_travel_sites) {
        const auto target = native::target_from_hex(image_base + site.rva, site.bytes);
        const auto reason = native::validate_target(memory, binding.image, target, stop, deadline);
        if (reason) { fast_travel_valid = false; break; }
    }
    const hooks::HookSpec fast_travel_specs[]{hooks::bind(image_base + rva_fast_travel_checkpoint_render,
        fast_travel_checkpoint_render_detour_entry, original_fast_travel_checkpoint_render)};
    const bool fast_travel_installed = fast_travel_valid && fast_travel_hooks.install(fast_travel_specs, stop);
    installation_trace.record(save::BStage::profile_publish,
        fast_travel_installed ? save::BStatus::succeeded : save::BStatus::refused, "fast_travel_checkpoint_guard", 0,
        {{"validated", fast_travel_valid}, {"installed", fast_travel_installed}});
}
struct HudAdapterBinder {
    engine::LocalMemory& memory;
    const engine::Binding& binding;
    HANDLE stop;
    uint64_t deadline;
    template<class T> auto operator()(T& function, const char* name, uint32_t rva, size_t leaf, const char* bytes) const {
            const auto site = native::target_from_hex(image_base + rva, bytes);
            const auto reason = leaf ? native::validate_leaf_target(memory, binding.image, site, leaf, stop, deadline) :
                                       native::validate_target(memory, binding.image, site, stop, deadline);
            if (reason) {
                hud_trace.record(save::BStage::profile_prepare, save::BStatus::refused, name, 0, {{"rva", rva}, {"reason", reason},
                              {"graphics_ready", hud::graphics_ready}, {"keycap_ready", hud::keycap_ready}});
                return false;
            }
            function = reinterpret_cast<std::decay_t<decltype(function)>>(site.address);
            return true;
        }
};
template<class Bind> bool bind_hud_lookup(const Bind& bind_hud) {
    return bind_hud(hud::swf.lookup, "lookup", 0x185c150, 0, "40534883ec20488b4928488bda488b01ff5028488bc34883c4205bc3cccccccc") &&
bind_hud(hud::swf.sprite, "sprite", 0x184e470, 0, "40534883ec20833908752b488b59084885db74224c8b03488bcb488b150f5c06") &&
bind_hud(hud::swf.release, "release", 0x184e3b0, 0, "4883ec288b0183f8027527488b4908b8fffffffff00fc1413083f80175544885") &&
bind_hud(hud::swf.string_init, "string_init", 0x3fa8e0, 39, "488d0591cb6602c7411414000080488901488d411848894108c7411000000000") &&
bind_hud(hud::swf.string_set, "string_set", 0x3faff0, 0, "48895c2410488974241848897c242041564883ec304c8bf2488bd94885d20f85") &&
bind_hud(hud::swf.string_free, "string_free", 0x3facb0, 0, "40534883ec20488b5108488d05b7c76602488901488bd94885d2742d8b4114c1");
}
template<class Bind> bool bind_hud_timeline(const Bind& bind_hud) {
    return bind_hud(hud::swf.duplicate, "duplicate", 0x1859a30, 0, "4883ec28488b41404c8bc24885c0751f488b4130488d0df53c6001488b500848") &&
bind_hud(hud::swf.entry, "entry", 0x1859db0, 0, "48895c241057448b51784533db8bfa488bd9458bcb4585d27e35488974241048") &&
bind_hud(hud::swf.add, "add", 0x1856c70, 0, "48895c2418555657415541564883ec7033f6488bd948634978458bf04d8be98b") &&
bind_hud(hud::swf.dirty, "dirty", 0x1857110, 0, "4c8bdc574883ec70488b05b97895024833c448894424584863410c488bf983f8") &&
bind_hud(hud::swf.start, "start", 0x18610d0, 0, "48895c2408574883ec200fb741588bfa488bd93bd074447d0de8928effff488b") &&
bind_hud(hud::swf.frame, "frame", 0x1865280, 0, "48895c2408574883ec200fb74158bf010000003bd7488bd90f4ffa3bf8742c7d") &&
bind_hud(hud::swf.label, "label", 0x1859e20, 0, "48895c2420555641564883ec60488b05a44b95024833c448894424580fb60245");
}
template<class Bind> bool bind_hud_artwork(const Bind& bind_hud) {
    return bind_hud(hud::swf.visible, "visible", 0x1864430, 97, "440fb6d23851517457807952007551488b41104c6349088851514d03c9488b10") &&
bind_hud(hud::swf.position, "position", 0x1863ec0, 70, "48837940004c8bc9743b488b41104c63410c49c1e006488b9080000000f3410f") &&
bind_hud(hud::swf.color, "color", 0x1863d90, 0, "48896c24104889742418574883ec308bf2488bf981fa0d0100000f87f7000000") &&
bind_hud(hud::swf.material, "material", 0x1863c30, 0, "48895c240848896c24104889742418574883ec20488bd9418bf1488b4960418b") &&
bind_hud(hud::swf.find_material, "find_material", 0x17aa5d0, 0, "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448");
}
void install_hud_presentation(const engine::Binding& binding, engine::LocalMemory& memory, HANDLE stop, uint64_t deadline) {
    const auto& presentation_sites = builds::steam_20260818().presentation;
    bool presentation_valid = true;
    for (const auto& site : presentation_sites) {
        const auto target = native::target_from_hex(image_base + site.rva, site.bytes);
        const auto error = native::validate_target(memory, binding.image, target, stop, GetTickCount64() + 10000);
        installation_trace.record(save::BStage::profile_prepare, error ? save::BStatus::refused : save::BStatus::entered,
            "special_hud_site", 0, {{"rva", site.rva}, {"validation", error}});
        if (error) { presentation_valid = false; break; }
    }
    if (presentation_valid) {
        const HudAdapterBinder bind_hud{memory,binding,stop,deadline};
        hud::graphics_ready = bind_hud_lookup(bind_hud) && bind_hud_timeline(bind_hud) && bind_hud_artwork(bind_hud) &&
            binding.image.contains(0x5e05200, sizeof(uintptr_t), IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_EXECUTE);
        hud::keycap_ready = hud::graphics_ready &&
            bind_hud(hud::swf.text, "text", 0x184e4b0, 0, "40534883ec20833908752b488b59084885db74224c8b03488bcb488b15d75b06") &&
            bind_hud(hud::swf.set_text, "set_text", 0x186db00, 0, "40534883ec20488bd94883c140e8ded4b8fe488bcb4883c4205be9e1cbffffcc");
        hud::keycap_ready = hud::keycap_ready && bind_hud(hud::swf.keycap, "keycap", 0xfb4ae0, 0,
                "40534883ec50488b491833c0894424604885c9743e48894424384c8d0d1b62d4");
        hud_trace.record(save::BStage::profile_prepare, save::BStatus::entered,
            "hud_adapters_ready", 0, {{"graphics_ready", hud::graphics_ready}, {"keycap_ready", hud::keycap_ready}});
        project_crucible_hud = reinterpret_cast<WeaponHudProject>(image_base + 0xf0c230);
        project_hammer_hud = reinterpret_cast<WeaponHudProject>(image_base + 0xf0c640);
        const hooks::HookSpec hud_specs[]{
            hooks::bind(reinterpret_cast<uintptr_t>(hud::swf.label), weapon_info_label_detour_entry, original_weapon_info_label),
            hooks::bind(image_base + 0xf0b3b0, weapon_hud_update_detour_entry, original_weapon_hud_update)};
        const bool installed = hud::graphics_ready && hud_hooks.install(hud_specs, stop);
        if (installed) hud::swf.label = weapon_info_label_original;
        installation_trace.record(save::BStage::profile_prepare, installed ? save::BStatus::succeeded : save::BStatus::refused, "special_hud_install", 0,
            {{"installed", installed}});
    }
}
void install_chainsaw_observation(const engine::Binding& binding, engine::LocalMemory& memory, HANDLE stop) {
    constexpr char chainsaw_bytes[]="4053565741544881ecc8000000488b05941fd7024833c4488984249000000048";
    const auto chainsaw = native::target_from_hex(image_base+0x143ca30, chainsaw_bytes);
    const auto chainsaw_validation=native::validate_target(memory,binding.image,chainsaw,stop,GetTickCount64()+10000);
    const hooks::HookSpec chainsaw_specs[]{hooks::bind(chainsaw.address, chainsaw_tick_detour_entry, original_chainsaw_tick)};
    const bool chainsaw_installed = !chainsaw_validation && chainsaw_hooks.install(chainsaw_specs, stop);
    installation_trace.record(save::BStage::chainsaw_state,
        chainsaw_installed ? save::BStatus::succeeded : save::BStatus::refused, "chainsaw_diagnostic_install", 0,
        {{"validation", chainsaw_validation}, {"installed", chainsaw_installed}});
}
}
void install(const engine::Binding& binding, HANDLE stop) {
    installation_trace.record(save::BStage::native_start, save::BStatus::entered, "validating");
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;

    engine::LocalMemory memory;
    if (!install_required_sites(binding, memory)) return;
    install_hands_restore(binding, stop);

    if (!install_upgrade_vtable(binding, memory)) return;
    install_selection_routes(binding, memory, stop);
    install_upgrade_sites(binding, memory);
    install_loot_observation(binding, memory, stop);
    const auto deadline=GetTickCount64()+10000;
    const bool hud_installed=install_hud_capture(binding, memory, stop, deadline);
    install_fast_travel_guard(binding, memory, stop, deadline);
    install_hud_presentation(binding, memory, stop, deadline);
    install_chainsaw_observation(binding, memory, stop);
    ready.store(true, std::memory_order_release);
    installation_trace.record(save::BStage::native_start, save::BStatus::succeeded, "ownership_ready", 0,
        {{"selection_route_ready", route_ready.load(std::memory_order_acquire)},
         {"hud_hook", hud_installed}});
}


} // namespace sentinel::special
