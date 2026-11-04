#include "challenge_seam_layout.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL challenge_seam:%d: %s\n",__LINE__,#x); std::abort(); } } while (0)
namespace {
bool suppress = false;
uintptr_t expected_player = 0;
int32_t expected_delta = 0;
bool predicate(uintptr_t player, uint32_t currency, int32_t delta, uint8_t notify, uintptr_t caller) {
    CHECK(player == expected_player && currency == 2 && delta == expected_delta && notify == 1);
    CHECK(caller == 0x12345678);
    return suppress;
}
}
int main() {
    using namespace sentinel::challenge::seam;
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)); CHECK(code);
    const uint8_t entry[]{0x53,0x56,0x55,0x41,0x56,0x48,0x83,0xec,0x48,
        0x48,0x89,0xcb,0x89,0xd6,0x44,0x89,0xc5,0x45,0x88,0xce,
        0x48,0xc7,0x44,0x24,0x38,0x78,0x56,0x34,0x12,0xff,0x25,0,0,0,0};
    const uint8_t continuation[]{0xb8,0x42,0,0,0,0x48,0x83,0xc4,0x48,0x41,0x5e,0x5d,0x5e,0x5b,0xc3};
    std::memcpy(code, entry, sizeof(entry));
    const auto adapter = reinterpret_cast<uintptr_t>(code + 128);
    std::memcpy(code + sizeof(entry), &adapter, sizeof(adapter));
    std::memcpy(code + 320, continuation, sizeof(continuation));
    write_adapter(code + 128, reinterpret_cast<uintptr_t>(predicate), reinterpret_cast<uintptr_t>(code + 256),
                  reinterpret_cast<uintptr_t>(code + 320));
    write_replay(code + 256, reinterpret_cast<uintptr_t>(code + 320));
    DWORD previous = 0; CHECK(VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &previous));
    CHECK(FlushInstructionCache(GetCurrentProcess(), code, 4096));
    std::vector<uint8_t> player(0x4ce00);
    expected_player = reinterpret_cast<uintptr_t>(player.data());
    const auto call = reinterpret_cast<int(*)(uintptr_t,uint32_t,int32_t,uint8_t)>(code);
    const auto balance = [&] { int32_t value = 0; std::memcpy(&value, player.data()+0x4cd6c+8, 4); return value; };
    expected_delta = 11; CHECK(call(expected_player, 2, expected_delta, 1) == 0x42 && balance() == 11);
    suppress = true; expected_delta = 99; CHECK(call(expected_player, 2, expected_delta, 1) == 0x42 && balance() == 11);
    suppress = false; expected_delta = -7; CHECK(call(expected_player, 2, expected_delta, 1) == 0x42 && balance() == 4);
    CHECK(VirtualFree(code, 0, MEM_RELEASE));
    std::puts("PASS seam preserves native arguments and replays the mutation without a MinHook trampoline");
}
