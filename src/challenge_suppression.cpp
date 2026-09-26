#include "challenge_suppression.h"
#include "challenge_seam_layout.h"
#include "native_target.h"
#include "native_runtime.h"
#include "save_session.h"
#include "weapon_points.h"
#include "MinHook.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace sentinel::challenge {
namespace {

std::atomic<bool> ready{false};
std::atomic<bool> install_attempted{false};
std::atomic<bool> first_candidate_mismatch_recorded{false};
uintptr_t image_base = 0, engine_root = 0;

// Scoped native completion context for one 1474a00 invocation. The native call
// chain is synchronous, so the innermost scope owns every nested currency
// transaction and the previous scope is restored on normal and exceptional
// exits. Witnesses are the declaration pointers passed through the live
// mission group scan for this invocation.
struct Scope {
    bool active = false;
    bool admitted = false;
    bool map_qualified = false;
    bool record_mission = false;
    uint64_t epoch = 0;
    uint32_t thread = 0;
    uint32_t owner_thread = 0;
    uintptr_t manager = 0, player = 0, record = 0, decl = 0, map = 0;
    uint32_t witness_count = 0;
    uintptr_t witnesses[max_member_witnesses]{};
};
thread_local Scope scope{};

using ChallengeUpdate = uint8_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using ChallengeRead = uint8_t (*)(uintptr_t, uintptr_t);
ChallengeUpdate original_update = nullptr;
ChallengeRead original_read = nullptr;
uint8_t* adapter = nullptr;

void hex_bytes(const char* hex, std::array<uint8_t, 32>& out) {
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    out.fill(0);
    for (size_t n = 0; hex[n * 2] && hex[n * 2 + 1]; ++n)
        out[n] = static_cast<uint8_t>(digit(hex[n * 2]) * 16 + digit(hex[n * 2 + 1]));
}

bool campaign_map(const sc_native_snapshot& snapshot) {
    constexpr char prefix[] = "game/sp/";
    const auto& map = snapshot.current_map;
    return map.validity == SC_OBSERVATION_OBSERVED && map.length >= sizeof(prefix) - 1 &&
        !std::memcmp(map.bytes, prefix, sizeof(prefix) - 1);
}

void capture_scope(uintptr_t manager, uintptr_t record) {
    Scope next{};
    next.manager = manager;
    next.record = record;
    next.thread = GetCurrentThreadId();
    engine::LocalMemory memory;
    uintptr_t decl = 0, player = 0, map = 0, map_identity = 0;
    uint32_t flags = 0;
    if (!manager || !record ||
        memory.copy(record, &decl, sizeof(decl)).reason || !decl ||
        memory.copy(decl + 0x9c, &flags, sizeof(flags)).reason ||
        memory.copy(manager + 0x10, &player, sizeof(player)).reason || !player ||
        memory.copy(engine_root + 0x50, &map, sizeof(map)).reason || !map ||
        memory.copy(map, &map_identity, sizeof(map_identity)).reason ||
        map_identity != image_base + 0x2ab30c8) return;
    const bool admitted = native::gameplay_admitted();
    if (!admitted) return;
    const auto snapshot = native::inspect();
    next.decl = decl;
    next.player = player;
    next.map = map;
    next.record_mission = (flags & 2) != 0;
    next.epoch = native::observation_stamp();
    next.admitted = admitted;
    next.owner_thread = snapshot.native_owner_thread_id;
    next.map_qualified = admitted && next.epoch != 0 && snapshot.context_generation != 0 &&
        snapshot.game_state == SC_GAME_IN_GAME && campaign_map(snapshot);
    next.active = true;
    scope = next;
}

uint8_t update_hook(uintptr_t manager, uintptr_t record, uintptr_t p3, uintptr_t p4, uintptr_t p5) {
    const Scope prior = scope;
    scope = Scope{};
    capture_scope(manager, record);
    uint8_t result = 0;
    __try { result = original_update(manager, record, p3, p4, p5); }
    __finally { scope = prior; }
    return result;
}

uint8_t read_hook(uintptr_t manager, uintptr_t decl) {
    if (scope.active && manager == scope.manager && decl) {
        bool seen = false;
        for (uint32_t i = 0; i < scope.witness_count && !seen; ++i)
            seen = scope.witnesses[i] == decl;
        if (!seen && scope.witness_count < max_member_witnesses)
            scope.witnesses[scope.witness_count++] = decl;
    }
    return original_read(manager, decl);
}

// The canonical aggregate is exactly the authored three-member mission group:
// the two other member declarations witnessed by the live group scan plus the
// completing declaration, each a distinct live mission-flagged declaration.
bool canonical_group(const Scope& current) {
    if (!current.manager || !current.decl) return false;
    uintptr_t decls[canonical_group_members]{};
    uint32_t distinct = 0;
    for (uint32_t i = 0; i < current.witness_count; ++i) {
        const uintptr_t decl = current.witnesses[i];
        if (!decl || decl == current.decl) continue;
        bool duplicate = false;
        for (uint32_t n = 0; n < canonical_group_members - 1; ++n)
            duplicate = duplicate || decls[n] == decl;
        if (duplicate) continue;
        if (distinct < canonical_group_members - 1) decls[distinct] = decl;
        ++distinct;
    }
    if (distinct != canonical_group_members - 1) return false;
    decls[canonical_group_members - 1] = current.decl;
    engine::LocalMemory memory;
    for (uint32_t i = 0; i < canonical_group_members; ++i) {
        uint32_t flags = 0;
        if (memory.copy(decls[i] + 0x9c, &flags, sizeof(flags)).reason || !(flags & 2)) return false;
        for (uint32_t n = 0; n < i; ++n)
            if (decls[n] == decls[i]) return false;
    }
    return true;
}

uintptr_t current_map() {
    engine::LocalMemory memory;
    uintptr_t map = 0;
    return memory.copy(engine_root + 0x50, &map, sizeof(map)).reason ? 0 : map;
}

bool evaluate(uintptr_t player, uint32_t currency, int32_t delta, uint8_t notify, uintptr_t return_site) {
    const Scope current = scope;
    ScopeFacts facts{};
    facts.active = current.active;
    facts.admitted = current.admitted;
    facts.map_qualified = current.map_qualified;
    facts.record_mission = current.record_mission;
    facts.canonical_group = canonical_group(current);
    facts.epoch = current.epoch;
    facts.thread = current.thread;
    facts.owner_thread = current.owner_thread;
    facts.player = current.player;
    facts.map = current.map;
    CallFacts call{};
    call.thread = GetCurrentThreadId();
    call.admitted = native::gameplay_admitted();
    call.session_admitted = save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests();
    call.epoch = native::observation_stamp();
    call.player = player;
    call.map = current_map();
    call.return_site = weapon_points::currency_origin(return_site);
    call.expected_return_site = image_base + 0x147546e;
    call.currency = currency;
    call.delta = delta;
    call.notify = notify;
    const char* mismatch = suppression_mismatch(facts, call);
    if (mismatch && suppression_diagnostic_candidate(facts, call) &&
        !first_candidate_mismatch_recorded.exchange(true, std::memory_order_acq_rel)) {
        const auto scope_flags = static_cast<uint32_t>(facts.active) |
            (static_cast<uint32_t>(facts.admitted) << 1) |
            (static_cast<uint32_t>(facts.map_qualified) << 2) |
            (static_cast<uint32_t>(facts.record_mission) << 3) |
            (static_cast<uint32_t>(facts.canonical_group) << 4);
        const auto call_flags = static_cast<uint32_t>(call.admitted) |
            (static_cast<uint32_t>(call.session_admitted) << 1);
        save::session().btrace.record(save::BStage::challenge_suppression, save::BStatus::refused,
            mismatch, 0, {{"scope_flags", scope_flags}, {"call_flags", call_flags},
            {"scope_epoch", facts.epoch}, {"scope_thread", facts.thread},
            {"scope_owner_thread", facts.owner_thread}, {"scope_player", facts.player},
            {"scope_map", facts.map}, {"call_epoch", call.epoch}, {"call_thread", call.thread},
            {"call_player", call.player}, {"call_map", call.map}, {"return_site", call.return_site},
            {"expected_return_site", call.expected_return_site}, {"currency", call.currency},
            {"delta", call.delta}, {"notify", call.notify}});
    }
    if (!mismatch)
        save::session().btrace.record(save::BStage::challenge_suppression, save::BStatus::succeeded,
            "aggregate_battery_suppressed", 0,
            {{"epoch", call.epoch}, {"player", call.player}, {"return_site", call.return_site},
             {"currency", call.currency}, {"delta", call.delta}, {"members", canonical_group_members}});
    return mismatch == nullptr;
}

bool seam_predicate(uintptr_t player, uint32_t currency, int32_t delta, uint8_t notify, uintptr_t return_site) {
    __try { return evaluate(player, currency, delta, notify, return_site); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

native::Target target(uintptr_t base, uint32_t offset, const char* hex) {
    native::Target out{};
    out.address = base + offset;
    hex_bytes(hex, out.bytes);
    return out;
}

// Leaf entries carry no unwind record, so uniqueness of the exact entry window
// is established directly instead of through validate_target.
bool unique_leaf(engine::Memory& memory, const engine::Image& image, uintptr_t address,
                 const char* hex, HANDLE stop, uint64_t deadline) {
    std::array<uint8_t, 32> expected{};
    hex_bytes(hex, expected);
    if (address < image.base || address - image.base > UINT32_MAX) return false;
    const auto rva = static_cast<uint32_t>(address - image.base);
    if (!image.contains(rva, expected.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE))
        return false;
    std::array<uint8_t, 32> actual{};
    if (memory.copy(address, actual.data(), actual.size()).reason || actual != expected) return false;
    std::array<uint8_t, 65536> chunk{};
    unsigned matches = 0;
    for (const auto& section : image.sections) {
        if (!(section.flags & IMAGE_SCN_MEM_EXECUTE)) continue;
        for (size_t offset = 0; offset + expected.size() <= section.size;) {
            if (GetTickCount64() >= deadline || (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT)) return false;
            const auto count = std::min(chunk.size(), static_cast<size_t>(section.size) - offset);
            if (memory.copy(image.base + section.rva + offset, chunk.data(), count).reason) return false;
            auto cursor = chunk.begin();
            const auto end = chunk.begin() + count;
            while ((cursor = std::search(cursor, end, expected.begin(), expected.end())) != end) {
                if (++matches > 1) return false;
                ++cursor;
            }
            if (count <= expected.size()) break;
            offset += count - expected.size() + 1;
        }
    }
    return matches == 1;
}

bool bytes_match(engine::Memory& memory, uintptr_t address, const char* hex, size_t length) {
    std::array<uint8_t, 32> expected{};
    hex_bytes(hex, expected);
    std::array<uint8_t, 32> actual{};
    return !memory.copy(address, actual.data(), length).reason &&
        !std::memcmp(actual.data(), expected.data(), length);
}

bool call_site(engine::Memory& memory, uintptr_t base, uint32_t after, uint32_t callee) {
    uint8_t opcode = 0;
    int32_t relative = 0;
    return !memory.copy(base + after - 5, &opcode, 1).reason && opcode == 0xe8 &&
        !memory.copy(base + after - 4, &relative, 4).reason &&
        static_cast<int64_t>(after) + relative == callee;
}

} // namespace

bool available() { return ready.load(std::memory_order_acquire); }

void install(const engine::Binding& binding, HANDLE stop) {
    if (install_attempted.exchange(true, std::memory_order_acq_rel)) return;
    image_base = binding.image.base;
    engine_root = binding.root;

    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
    if (native::validate_target(memory, binding.image, target(image_base, 0x1474a00,
            "48895c24185556574154415541564157488dac2400faffff4881ec0007000048"), stop, deadline)) return;
    if (!unique_leaf(memory, binding.image, image_base + 0x14703b0,
            "4c8bd24c8bc94885d20f8485000000488b4a084533c00fb60184c074180f1f00", stop, deadline)) return;
    if (!binding.image.contains(seam::currency_entry_rva, seam::wup_entry_patch_bytes,
            IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_WRITE)) return;
    if (!native::function_window(memory, binding.image, image_base + seam::currency_entry_rva,
            image_base + seam::currency_seam_rva, 16) ||
        !native::function_window(memory, binding.image, image_base + seam::currency_entry_rva,
            image_base + seam::currency_continuation_rva, 16) ||
        !native::function_window(memory, binding.image, image_base + 0x1474a00, image_base + 0x1475452, 23) ||
        !native::function_window(memory, binding.image, image_base + 0x1474a00, image_base + 0x147546e, 16)) return;
    if (!bytes_match(memory, image_base + seam::currency_seam_rva, "440184b16ccd0400488b05a2832b0383", 16) ||
        !bytes_match(memory, image_base + seam::currency_continuation_rva, "488d8be05a02008bd6e8888aebff83fe", 16) ||
        !bytes_match(memory, image_base + 0x1475452,
            "488b4e104533c9c7442420ffffffff418d5104458d4101", 23) ||
        !bytes_match(memory, image_base + 0x147546e, "418b953c01000083faff741b488b4e10", 16) ||
        !call_site(memory, image_base, 0x147546e, seam::currency_entry_rva)) return;

    adapter = static_cast<uint8_t*>(VirtualAlloc(nullptr, seam::adapter_size,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!adapter) return;
    void* trampoline = nullptr;
    if (MH_CreateHook(reinterpret_cast<void*>(image_base + seam::currency_seam_rva), adapter, &trampoline) != MH_OK) return;
    seam::write_adapter(adapter, reinterpret_cast<uintptr_t>(&seam_predicate),
        reinterpret_cast<uintptr_t>(trampoline), image_base + seam::currency_continuation_rva);
    DWORD previous = 0;
    if (!VirtualProtect(adapter, seam::adapter_size, PAGE_EXECUTE_READ, &previous)) return;
    FlushInstructionCache(GetCurrentProcess(), adapter, seam::adapter_size);
    if (MH_CreateHook(reinterpret_cast<void*>(image_base + 0x1474a00),
            reinterpret_cast<void*>(update_hook), reinterpret_cast<void**>(&original_update)) != MH_OK) return;
    if (MH_CreateHook(reinterpret_cast<void*>(image_base + 0x14703b0),
            reinterpret_cast<void*>(read_hook), reinterpret_cast<void**>(&original_read)) != MH_OK) return;
    if (MH_EnableHook(reinterpret_cast<void*>(image_base + 0x1474a00)) != MH_OK) return;
    if (MH_EnableHook(reinterpret_cast<void*>(image_base + 0x14703b0)) != MH_OK) return;
    if (MH_EnableHook(reinterpret_cast<void*>(image_base + seam::currency_seam_rva)) != MH_OK) return;
    ready.store(true, std::memory_order_release);
}

} // namespace sentinel::challenge
