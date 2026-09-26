// Mid-function adapter for the hardcoded Sentinel Battery grant at image+0x13ce4df.
// MinHook redirects the first mutation instruction there; this adapter evaluates
// the AP suppression predicate with the native seam state intact and then either
// tail-jumps to the documented continuation or to the MinHook trampoline.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sentinel::challenge::seam {
// Entered with the native post-prologue state: RSP=S (16-byte aligned),
// RBX=player, RSI=currency, EBP=delta, R14B=notification suppression and the
// native return address at [S+0x38]. Every GPR and flag is saved around the
// predicate call; both exits restore the exact seam state and use a
// register-free indirect tail jump. XMM6-XMM15 are preserved by the predicate
// ABI; XMM0-XMM5 are volatile and unused by the continuation.
inline constexpr std::size_t adapter_size = 112;
inline constexpr std::size_t predicate_slot = 88;
inline constexpr std::size_t trampoline_slot = 96;
inline constexpr std::size_t continuation_slot = 104;
inline constexpr uint32_t currency_entry_rva = 0x13ce4c0;
inline constexpr uint32_t currency_seam_rva = 0x13ce4df;
inline constexpr uint32_t currency_continuation_rva = 0x13ce53a;
inline constexpr uint32_t wup_entry_patch_bytes = 5;
static_assert(currency_entry_rva + wup_entry_patch_bytes <= currency_seam_rva,
    "challenge seam window must remain outside the WUP entry patch");
static_assert(currency_entry_rva + wup_entry_patch_bytes <= currency_continuation_rva,
    "challenge continuation window must remain outside the WUP entry patch");
inline constexpr uint8_t adapter_template[adapter_size] = {
    0x9c, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
    0x48, 0x8b, 0x84, 0x24, 0x78, 0x00, 0x00, 0x00, 0x48, 0x83, 0xec, 0x30,
    0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0x89, 0xd9, 0x89, 0xf2, 0x41, 0x89,
    0xe8, 0x45, 0x0f, 0xb6, 0xce, 0xff, 0x15, 0x29, 0x00, 0x00, 0x00, 0x88,
    0x44, 0x24, 0x28, 0x48, 0x83, 0xc4, 0x30, 0x41, 0x5b, 0x41, 0x5a, 0x41,
    0x59, 0x41, 0x58, 0x5a, 0x59, 0x58, 0x80, 0x7c, 0x24, 0xc0, 0x00, 0x75,
    0x07, 0x9d, 0xff, 0x25, 0x10, 0x00, 0x00, 0x00, 0x9d, 0xff, 0x25, 0x11,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};
// The predicate is bool(uintptr_t player, uint32_t currency, int32_t delta,
// uint8_t notify, uintptr_t return_site) and is called with RCX/EDX/R8D/R9B
// plus the documented return site from [S+0x38] as the fifth argument.
inline void write_adapter(uint8_t* code, uintptr_t predicate, uintptr_t trampoline,
                          uintptr_t continuation) {
    std::memcpy(code, adapter_template, adapter_size);
    std::memcpy(code + predicate_slot, &predicate, sizeof(predicate));
    std::memcpy(code + trampoline_slot, &trampoline, sizeof(trampoline));
    std::memcpy(code + continuation_slot, &continuation, sizeof(continuation));
}
}
