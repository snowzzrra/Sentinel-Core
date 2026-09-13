#include "weapon_points.h"
#include "native_target.h"
#include "save_session.h"
#include "MinHook.h"
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <climits>

namespace sentinel::weapon_points {
namespace {
std::atomic<bool> ready{false};
std::atomic<uint64_t> suppressed{0};
uintptr_t image_base = 0, engine_root = 0;
uint32_t image_size = 0;
using Add = void(*)(uintptr_t, uint32_t, int32_t, uint8_t);
using Pair = void(*)(uintptr_t, const int32_t*, uint8_t, uint32_t);
using Append = void(*)(uintptr_t, const char*, const char*, uint32_t, uint64_t, uint32_t);
using Notify = void(*)(uintptr_t, uint32_t, uintptr_t);
using Exchange = void(*)(uintptr_t, uintptr_t);
Add original_add = nullptr;
Pair original_pair = nullptr;
Append original_append = nullptr;
Notify original_notify = nullptr;
Exchange original_exchange = nullptr;
thread_local Source pair_source = Source::unknown;
// Once AP routing owns the process, a later session fault must not turn vanilla
// minting back on. New AP execution still requires admitted/accepting below.
bool active() { return ready.load(std::memory_order_acquire) && save::session().routed(); }
uint32_t rva(void* address) {
    const auto p = reinterpret_cast<uintptr_t>(address);
    return p >= image_base && p-image_base < image_size ? static_cast<uint32_t>(p-image_base) : UINT32_MAX;
}
void add_hook(uintptr_t player, uint32_t currency, int32_t amount, uint8_t silent) {
    const auto caller = rva(_ReturnAddress());
    const auto source = caller == 0x13ce4b8 ? pair_source : direct_source(caller);
    if (suppress(active(), source, currency, amount, false)) {
        suppressed.fetch_add(1, std::memory_order_relaxed); return;
    }
    original_add(player, currency, amount, silent);
}
void pair_hook(uintptr_t player, const int32_t* pair, uint8_t silent, uint32_t source) {
    const auto prior = pair_source; pair_source = wrapper_source(rva(_ReturnAddress()));
    __try { original_pair(player, pair, silent, source); }
    __finally { pair_source = prior; }
}
void append_hook(uintptr_t self, const char* title, const char* reward, uint32_t time, uint64_t icon, uint32_t color) {
    // The only caller is the native corruption-cleared earnings presenter.
    // Preserve completion title, timing and icon; remove its false WUP subtitle.
    if (active() && rva(_ReturnAddress()) == 0xeeaa0a) reward = "";
    original_append(self, title, reward, time, icon, color);
}
void notify_hook(uintptr_t self, uint32_t code, uintptr_t data) {
    if (active() && code == 0x39) code = 0x3a; // Native Slayer Gate completion without points.
    original_notify(self, code, data);
}
void exchange_hook(uintptr_t self, uintptr_t player) {
    // Native menu action 0x31 exchanges WUP for Mastery Tokens. AP Mastery
    // ownership is independent, so this conversion is unavailable in AP.
    if (!active()) original_exchange(self, player);
}
native::Target target(uintptr_t base, uint32_t offset, const char* hex) {
    native::Target out{}; out.address = base+offset;
    const auto digit = [](char c) { return c <= '9' ? c-'0' : c-'a'+10; };
    for (size_t n = 0; n < out.bytes.size(); ++n)
        out.bytes[n] = static_cast<uint8_t>(digit(hex[n*2])*16+digit(hex[n*2+1]));
    return out;
}
bool leaf(engine::Memory& memory, const engine::Image& image, uint32_t offset, const char* bytes) {
    const auto t = target(image.base, offset, bytes); std::array<uint8_t, 32> actual{};
    return image.contains(offset, actual.size(), IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ, 0) &&
        !memory.copy(t.address, actual.data(), actual.size()).reason && actual == t.bytes;
}
bool call_site(engine::Memory& memory, uintptr_t base, uint32_t after, uint32_t callee) {
    uint8_t opcode = 0; int32_t relative = 0;
    return !memory.copy(base+after-5, &opcode, 1).reason && opcode == 0xe8 &&
        !memory.copy(base+after-4, &relative, 4).reason &&
        static_cast<int64_t>(after)+relative == callee;
}
uintptr_t player(void*) {
    __try {
        const auto map = *reinterpret_cast<uintptr_t*>(engine_root+0x50);
        return map ? reinterpret_cast<uintptr_t(*)(uintptr_t,uint32_t)>(image_base+0x69af70)(map,0) : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool read(void*, uintptr_t p, uint32_t& balance, uint32_t& gained) {
    __try {
        using Get = int32_t(*)(uintptr_t,uint32_t);
        const auto b = reinterpret_cast<Get>(image_base+0x13e8680)(p,0);
        const auto g = reinterpret_cast<Get>(image_base+0x13e8690)(p,0);
        if (b < 0 || g < 0) return false;
        balance = static_cast<uint32_t>(b); gained = static_cast<uint32_t>(g); return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint32_t grant(void*, uintptr_t p, uint32_t amount) {
    uint32_t error = 0;
    // The typed grant calls the validated trampoline directly. Nested vanilla
    // rewards retain their own provenance; they do not inherit an AP bypass.
    __try { original_add(p, 0, static_cast<int32_t>(amount), 1); }
    __except(EXCEPTION_EXECUTE_HANDLER) { error = GetExceptionCode(); }
    return error;
}
bool refresh(void*, uintptr_t p) {
    __try {
        const auto hud = reinterpret_cast<uintptr_t(*)(uintptr_t)>(image_base+0x143c350)(p);
        if (!hud) return false;
        const auto id = *reinterpret_cast<int16_t*>(hud+0xc);
        const auto manager = *reinterpret_cast<uintptr_t*>(image_base+0x47dd908);
        if (id == -1 || !manager) return false;
        int32_t values[5]{};
        reinterpret_cast<int32_t*(*)(int32_t*,uintptr_t)>(image_base+0xf22f00)(values,p);
        reinterpret_cast<void(*)(uintptr_t,int16_t,uint16_t,const void*)>(image_base+0x17c1180)(manager,id,0x11e,values);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
Calls calls{nullptr, player, read, grant, refresh};
#ifdef SC_NATIVE_TESTING
char fixture_namespace[65]{};
#endif
}
bool available() { return ready.load(std::memory_order_acquire); }
bool admitted(const char* id) {
#ifdef SC_NATIVE_TESTING
    if (fixture_namespace[0]) return available() && !std::memcmp(id, fixture_namespace, 65);
#endif
    return active() && save::session().state() == save::SessionState::admitted &&
        save::session().accepts_requests() && !std::memcmp(id, save::session().namespace_id().c_str(), 65);
}
void execute_native(const sc_weapon_points_request& request, sc_weapon_points_result& out) {
    if (!admitted(request.namespace_id)) { out.outcome = SC_WUP_UNAVAILABLE; return; }
    out.flags |= SC_WUP_SUPPRESSION_ACTIVE;
    execute(request, out, calls); out.suppressed_grants = suppressed.load(std::memory_order_relaxed);
}
#ifdef SC_NATIVE_TESTING
void use_fixture(Calls value, const char* id) {
    calls = value; std::memcpy(fixture_namespace, id, 65); ready.store(true, std::memory_order_release);
}
#endif
void install(const engine::Binding& binding, HANDLE stop) {
    // Called by the existing pinned native owner; no second lifecycle/queue.
    image_base = binding.image.base; image_size = binding.image.size; engine_root = binding.root;
    engine::LocalMemory memory;
    struct Site { uint32_t offset; const char* bytes; void* detour; void** original; };
    const Site sites[] = {
        {0x13ce4c0,"48895c241048896c2418565741564883ec204863f2450fb6f1418be8488bd944",reinterpret_cast<void*>(add_hook),reinterpret_cast<void**>(&original_add)},
        {0x13ce4a0,"4883ec3844894c2420450fb6c8448b42048b12e8080000004883c438c3cccccc",reinterpret_cast<void*>(pair_hook),reinterpret_cast<void**>(&original_pair)},
        {0xeea070,"48895c240848896c2410488974241848897c242041564883ec20488db9700600",reinterpret_cast<void*>(append_hook),reinterpret_cast<void**>(&original_append)},
        {0x1273710,"48895c2408574881ec5004000048634108488bf94c894424304533c089542420",reinterpret_cast<void*>(notify_hook),reinterpret_cast<void**>(&original_notify)},
        {0x142d830,"48895c2418574883ec50488b059711d8024833c44889442448ff8274cd040048",reinterpret_cast<void*>(exchange_hook),reinterpret_cast<void**>(&original_exchange)},
        {0x143c350,"40534883ec20488b01488bd9ff907808000084c0740833c04883c4205bc3488b",nullptr,nullptr},
        {0xf22f00,"48895c2408574883ec20488bda488bf9488bcb33d2e866574c00ba0100000089",nullptr,nullptr},
        {0x17c1180,"405541574883ec28410fb7e84d8bf94c8b010fb7c2413b80100608000f8dbb00",nullptr,nullptr},
    };
    const auto deadline = GetTickCount64()+10000;
    for (const auto& s : sites)
        if (native::validate_target(memory,binding.image,target(image_base,s.offset,s.bytes),stop,deadline)) return;
    if (!leaf(memory,binding.image,0x69af70,"488bc183fa0b77104863ca488b8cc8f81a0000e97851a70133c0c3cccccccccc") ||
        !leaf(memory,binding.image,0x13e8680,"4863c28b84816ccd0400c3cccccccccc4863c28b848190cd0400c3cccccccccc") ||
        !leaf(memory,binding.image,0x13e8690,"4863c28b848190cd0400c3cccccccccc4883ec084c8b91f8f402004c63da4d85")) return;
    constexpr uint32_t direct[] = {0xb97fc5,0xb98238,0xb98497,0xb98779,0xb98ef7,
        0xc82e05,0xc82ad5,0xc82c02,0xd6d273,0xd6d4b4,0x105cbf3,0x147546e,0x13ce4b8};
    constexpr uint32_t pairs[] = {0x1474d7f,0x143404f,0xc98c2c,0xd0feee,0xd101ff};
    for (auto site : direct) if (!call_site(memory,image_base,site,0x13ce4c0)) return;
    for (auto site : pairs) if (!call_site(memory,image_base,site,0x13ce4a0)) return;
    if (!call_site(memory,image_base,0xeeaa0a,0xeea070)) return;
    for (const auto& s : sites) if (s.detour) {
        if (MH_CreateHook(reinterpret_cast<void*>(image_base+s.offset),s.detour,s.original) != MH_OK) return;
    }
    for (const auto& s : sites) if (s.detour)
        if (MH_EnableHook(reinterpret_cast<void*>(image_base+s.offset)) != MH_OK) return;
    ready.store(true,std::memory_order_release);
}
}
