#pragma once
#include "sentinel_weapon_points.h"
#include "engine_observer.h"

namespace sentinel::weapon_points {
enum class Source { unknown, encounter, pickup, target, script, unlockable, devinv, slayer_gate, useable };
Source direct_source(uint32_t return_rva);
Source wrapper_source(uint32_t return_rva);
bool suppress(bool ap_active, Source, uint32_t currency, int32_t delta, bool ap_origin);
bool valid(const sc_weapon_points_request&);
bool same(const sc_weapon_points_request&, const sc_weapon_points_request&);
sc_weapon_points_result initial(const sc_weapon_points_request&);

// Internal production seam; never externally supplied or serialized.
struct Calls {
    void* context = nullptr;
    uintptr_t (*player)(void*) = nullptr;
    bool (*read)(void*, uintptr_t, uint32_t&, uint32_t&) = nullptr;
    uint32_t (*grant)(void*, uintptr_t, uint32_t) = nullptr;
    bool (*refresh)(void*, uintptr_t) = nullptr;
};
void execute(const sc_weapon_points_request&, sc_weapon_points_result&, const Calls&);
void install(const engine::Binding&, HANDLE stop);
bool available();
bool admitted(const char* namespace_id);
void execute_native(const sc_weapon_points_request&, sc_weapon_points_result&);
#ifdef SC_NATIVE_TESTING
void use_fixture(Calls, const char* namespace_id);
#endif
}
