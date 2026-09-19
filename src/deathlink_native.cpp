#include "deathlink.h"
#include "native_target.h"
#include "save_session.h"
#include "MinHook.h"
#include <atomic>
#include <cstring>
#include <algorithm>

namespace sentinel::deathlink {
namespace {

std::atomic<bool> ready{false};
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;

// Verified against the supported Steam image (9809708c). All addresses are RVAs.
constexpr uint32_t rva_player = 0x69af70;             // idGameLocal::GetPlayer(index)
constexpr uint32_t rva_damage_typeinfo = 0x1631e80;   // returns idDeclTypeInfo for damage
constexpr uint32_t rva_find_decl = 0x17aa5d0;         // FindDecl(typeinfo, path, flags)
constexpr uint32_t rva_protection = 0x1424150;        // native death-prevention/invulnerability state
constexpr uint32_t rva_player_death = 0x11feae0;      // idPlayerAnalyzer::PlayerDeath (true local death)
constexpr uint32_t rva_extra_life = 0xa9c230;         // idPlayerAccessibility TryUseExtraLife wrapper
constexpr uint32_t rva_player_damage_slot = 0x4e8;    // idPlayer vtable slot: native Damage pipeline

const char* const LETHAL_DAMAGE_PATH = "damage/code_referenced/crush";

using PlayerAt = uintptr_t(*)(uintptr_t, uint32_t);
using DamageTypeInfo = uintptr_t(*)();
using FindDecl = uintptr_t(*)(uintptr_t, const char*, int);
using Protection = char(*)(uintptr_t);
using PlayerDeath = void(*)(uintptr_t);
using TryExtraLife = char(*)(uintptr_t, uintptr_t);
using DamageCall = void(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, float, uintptr_t, uintptr_t);

PlayerDeath original_player_death = nullptr;
TryExtraLife original_extra_life = nullptr;

std::atomic<bool> application_active{false};
std::atomic<uint32_t> application_deaths{0};
std::atomic<uint32_t> application_extra_lives{0};

#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif

uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        return map ? reinterpret_cast<PlayerAt>(image_base + rva_player)(map, 0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void player_death_detour(uintptr_t self) {
    const auto now = GetTickCount64();
    if (application_active.load(std::memory_order_acquire)) {
        application_deaths.fetch_add(1, std::memory_order_relaxed);
        // Causal suppression: this true death belongs to the remote logical event.
        record_native_death(SC_DEATHLINK_CAUSE_REMOTE, SC_DEATHLINK_PROTECTION_NONE, now);
    } else {
        record_native_death(SC_DEATHLINK_CAUSE_LOCAL, SC_DEATHLINK_PROTECTION_NONE, now);
    }
    if (original_player_death) original_player_death(self);
}

char extra_life_detour(uintptr_t entity, uintptr_t parm) {
    char consumed = 0;
    if (original_extra_life) consumed = original_extra_life(entity, parm);
    if (consumed && application_active.load(std::memory_order_acquire))
        application_extra_lives.fetch_add(1, std::memory_order_relaxed);
    return consumed;
}

bool valid_code_pointer(uintptr_t address) {
    return address >= image_base && address - image_base < image_size;
}

uint32_t apply_lethal(void*, uintptr_t p, ApplicationOutcome& outcome) {
    if (!p) return 1;
    uint32_t error = 0;
    __try {
        const auto typeinfo = reinterpret_cast<DamageTypeInfo>(image_base + rva_damage_typeinfo)();
        if (!typeinfo) return 2;
        const auto decl = reinterpret_cast<FindDecl>(image_base + rva_find_decl)(typeinfo, LETHAL_DAMAGE_PATH, 0);
        if (!decl) return 3;
        const auto vtable = *reinterpret_cast<uintptr_t**>(p);
        if (!vtable) return 4;
        const auto damage = vtable[rva_player_damage_slot / 8];
        if (!valid_code_pointer(damage)) return 5;

        // Exactly one legitimate lethal event through Doom's own player damage
        // pipeline. Extra Life, Saving Throw and native invulnerability are all
        // free to intercept it; nothing is bypassed here.
        float direction[3] = {0.0f, 0.0f, 1.0f};
        const auto before_deaths = application_deaths.load(std::memory_order_relaxed);
        const auto before_lives = application_extra_lives.load(std::memory_order_relaxed);
        application_active.store(true, std::memory_order_release);
        reinterpret_cast<DamageCall>(damage)(p, 0, 0, decl + 0x90, 1.0f,
                                             reinterpret_cast<uintptr_t>(direction), 0);
        application_active.store(false, std::memory_order_release);
        outcome.true_death = application_deaths.load(std::memory_order_relaxed) - before_deaths;
        outcome.extra_life = application_extra_lives.load(std::memory_order_relaxed) - before_lives;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        application_active.store(false, std::memory_order_release);
        error = GetExceptionCode();
    }
    return error;
}

bool protection_active(void*, uintptr_t p) {
    if (!p) return false;
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        if (!map) return false;
        return reinterpret_cast<Protection>(image_base + rva_protection)(map + 0x35867) != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

native::Target target(uintptr_t base, uint32_t offset, const char* hex) {
    native::Target out{}; out.address = base + offset;
    const auto digit = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (size_t n = 0; n < out.bytes.size(); ++n)
        out.bytes[n] = static_cast<uint8_t>(digit(hex[n * 2]) * 16 + digit(hex[n * 2 + 1]));
    return out;
}

bool matches(engine::Memory& memory, const engine::Image& image, const native::Target& t) {
    std::array<uint8_t, 32> actual{};
    const auto rva = static_cast<uint32_t>(t.address - image.base);
    return image.contains(rva, actual.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0) &&
        !memory.copy(t.address, actual.data(), actual.size()).reason && actual == t.bytes;
}
} // namespace

Calls calls{nullptr, player, apply_lethal, protection_active};

bool available() { return ready.load(std::memory_order_acquire); }

bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}

void execute_native(const sc_deathlink_request& request, sc_deathlink_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_DEATHLINK_OUTCOME_UNAVAILABLE; return; }
    out.flags |= SC_DEATHLINK_FLAG_SHARED_STATE_BOUND;
    execute(request, out, calls);
}

#ifdef SC_NATIVE_TESTING
void use_fixture(Calls value, const char* id) {
    calls = value;
    std::memcpy(fixture_namespace, id, 65);
    ready.store(true, std::memory_order_release);
}
#endif

void install(const engine::Binding& binding, HANDLE stop) {
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;

    engine::LocalMemory memory;
    const native::Target required[] = {
        target(image_base, rva_find_decl, "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448"),
        target(image_base, rva_player, "488bc183fa0b77104863ca488b8cc8f81a0000e97851a70133c0c3cccccccccc"),
        target(image_base, rva_damage_typeinfo, "488d05e9660603c3cccccccccccccccc488d0549f00603c3cccccccccccccccc"),
    };
    for (const auto& t : required) if (!matches(memory, binding.image, t)) return;

    const auto deadline = GetTickCount64() + 10000;
    const native::Target hooks[] = {
        target(image_base, rva_player_death, "40574883ec50488b0543744503488bf9837808007413488d153bf0b501488d0d"),
        target(image_base, rva_extra_life, "40534883ec20488bda4885c9742f488b01ff90b804000084c07422488b0d1eb1"),
        target(image_base, rva_protection, "4883ec28e897fcffff4885c07424f30f1080140100000f57c90f2fc1770df30f"),
    };
    for (const auto& t : hooks) if (native::validate_target(memory, binding.image, t, stop, deadline)) return;

    if (MH_CreateHook(reinterpret_cast<void*>(image_base + rva_player_death),
                      reinterpret_cast<void*>(player_death_detour),
                      reinterpret_cast<void**>(&original_player_death)) != MH_OK) return;
    if (MH_CreateHook(reinterpret_cast<void*>(image_base + rva_extra_life),
                      reinterpret_cast<void*>(extra_life_detour),
                      reinterpret_cast<void**>(&original_extra_life)) != MH_OK) return;
    if (MH_EnableHook(reinterpret_cast<void*>(image_base + rva_player_death)) != MH_OK) return;
    if (MH_EnableHook(reinterpret_cast<void*>(image_base + rva_extra_life)) != MH_OK) return;
    ready.store(true, std::memory_order_release);
}

} // namespace sentinel::deathlink
