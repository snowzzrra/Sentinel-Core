#include "inventory.h"
#include "runes.h"
#include "native_target.h"
#include "save_session.h"
#include <atomic>
#include <cstring>
#include <algorithm>

namespace sentinel::inventory {
namespace {
std::atomic<bool> ready{false};
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;


uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root + 0x50);
        return map ? reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t)>(image_base + 0x69af70)(map, 0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool read(void*, uintptr_t p, SnapshotFacts& facts) {
    __try {
        facts = {};
        const auto inv = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + 0x763080)(p);
        if (!inv) return false;
        const auto count = *reinterpret_cast<int*>(inv + 8);
        const auto capacity = *reinterpret_cast<int*>(inv + 12);
        if (count < 0 || count > 4096 || capacity < count ||
            (count && !*reinterpret_cast<uintptr_t*>(inv))) return false;
        const auto type = reinterpret_cast<uintptr_t(*)()>(image_base + 0x1617cd0)();
        if (!type) return false;
        const char* const paths[] = {
            "weapon/player/shotgun", "weapon/player/double_barrel", "weapon/player/heavy_cannon",
            "weapon/player/chaingun", "weapon/player/plasma_rifle", "weapon/player/gauss_rifle",
            "weapon/player/rocket_launcher", "weapon/player/bfg", "weapon/player/chainsaw",
            "weapon/player/unmaykr"};
        uint32_t weapons = 0;
        for (unsigned i = 0; i < _countof(paths); ++i) {
            const auto decl = reinterpret_cast<uintptr_t(*)(uintptr_t, const char*, int)>(
                image_base + 0x17aa5d0)(type, paths[i], 1);
            if (!decl) return true;
            const auto path = *reinterpret_cast<const char**>(decl + 8);
            if (!path || std::strncmp(path, paths[i], std::strlen(paths[i]) + 1)) return true;
            if (reinterpret_cast<uintptr_t(*)(uintptr_t, uintptr_t)>(image_base + 0x1690660)(inv, decl))
                weapons |= 1u << i;
        }
        facts.weapons = weapons;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint32_t ensure_items(void*, uintptr_t, uint32_t, uint32_t, uint32_t, uint32_t) { return 1; }
uint32_t set_capacity(void*, uintptr_t, uint8_t, uint8_t, uint8_t) { return 1; }

bool refresh(void*, uintptr_t p) {
    if (!p) return false;
    __try {
        const auto hud = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base + 0x143c350)(p);
        if (!hud) return true;
        const auto id = *reinterpret_cast<int16_t*>(hud + 0xc);
        const auto manager = *reinterpret_cast<uintptr_t*>(image_base + 0x47dd908);
        if (id != -1 && manager) {
            int32_t values[5]{};
            reinterpret_cast<int32_t*(*)(int32_t*, uintptr_t)>(image_base + 0xf22f00)(values, p);
            reinterpret_cast<void(*)(uintptr_t, int16_t, uint16_t, const void*)>(image_base + 0x17c1180)(manager, id, 0x11e, values);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void bind_run_state(void*, uintptr_t) {}

Calls calls{nullptr, player, read, ensure_items, set_capacity, refresh, bind_run_state};

#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif
}

bool available() { return ready.load(std::memory_order_acquire); }

bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return available() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}

void execute_native(const sc_inventory_request& request, sc_inventory_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_INV_UNAVAILABLE; return; }
    if (request.kind != SC_INV_OBSERVE) { out.outcome = SC_INV_UNAVAILABLE; return; }
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
    (void)stop;
    image_base = binding.image.base;
    image_size = binding.image.size;
    engine_root = binding.root;
    ready.store(false, std::memory_order_release);
    engine::LocalMemory memory;
    const struct { uint32_t rva; const char* bytes; } sites[] = {
        {0x763080, "488d81d8350000c3ccccccccccccccccf20f1081b8280000f20f11028b81c028"},
        {0x69af70, "488bc183fa0b77104863ca488b8cc8f81a0000e97851a70133c0c3cccccccccc"},
        {0x1617cd0, "488d0569210803c3cccccccccccccccc4883ec28ba33000000b9d8000000e84d"},
        {0x17aa5d0, "405556574157488dac2448feffff4881ecb8020000488b05ec43a0024833c448"},
        {0x1690660, "48895c240848896c2410488974241848897c242041564883ec2033ff488bea4c"},
    };
    for (const auto& site : sites) {
        std::array<uint8_t, 32> expected{}, actual{};
        const auto digit = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
        for (size_t i = 0; i < expected.size(); ++i)
            expected[i] = static_cast<uint8_t>(digit(site.bytes[2 * i]) * 16 + digit(site.bytes[2 * i + 1]));
        if (!binding.image.contains(site.rva, expected.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0) ||
            memory.copy(image_base + site.rva, actual.data(), actual.size()).reason || actual != expected) return;
    }
    ready.store(true, std::memory_order_release);
}
}
