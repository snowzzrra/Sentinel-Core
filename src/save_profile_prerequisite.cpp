// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_profile_prerequisite.h"
#include <windows.h>
#include <cstring>
#include <new>

namespace sentinel::save {
namespace {
struct NativeControl { uint32_t strong, weak; uintptr_t object; DestroySaveData destroy; };
static_assert(sizeof(NativeControl) == 24 && offsetof(NativeControl, object) == 8 && offsetof(NativeControl, destroy) == 16);
struct PrerequisiteFuture : SaveFuture {
    Session& owner; ProfilePrerequisiteCalls calls; engine::LocalMemory memory;
    SaveReference catalog_data{}; SaveFuture* enumeration = nullptr; SaveFuture* profile = nullptr;
    std::string campaign; ULONGLONG deadline = GetTickCount64() + 10000;
    bool enumerated = false, terminal = false;
    PrerequisiteFuture(Session& s, const ProfilePrerequisiteCalls& c) : owner(s), calls(c) {}
    ~PrerequisiteFuture() {
        if (enumeration) enumeration->vtable->destroy(enumeration, 1);
        if (profile) profile->vtable->destroy(profile, 1);
        calls.catalog.release(&catalog_data);
    }
};
SaveFuture* destroy(SaveFuture* value, uint32_t) { delete static_cast<PrerequisiteFuture*>(value); return value; }
SaveResult* poll(SaveFuture* value, SaveResult* out, void* executor) {
    auto& future = *static_cast<PrerequisiteFuture*>(value);
    if (future.terminal) { *out = {1, 0, 0, 0}; return out; }
    bool allowed = false;
    try {
        std::string current;
        allowed = future.owner.native_io() && GetTickCount64() <= future.deadline &&
            read_campaign_prefix(future.memory, future.calls.catalog.image_base, current) &&
            steam_name_equal(current, future.campaign);
    } catch (const std::bad_alloc&) {}
    SaveResult result{0, 1, 1, 0};
    if (allowed && !future.enumerated) {
        future.enumeration->vtable->poll(future.enumeration, &result, executor);
        if (result.state == -1) { *out = result; return out; }
        allowed = result.state == 0 && result.outcome == 0 && result.value == 1;
        future.enumerated = allowed;
        future.enumeration->vtable->destroy(future.enumeration, 1); future.enumeration = nullptr;
        future.calls.catalog.release(&future.catalog_data);
    }
    if (allowed) {
        future.profile->vtable->poll(future.profile, &result, executor);
        if (result.state == -1) { *out = result; return out; }
        allowed = result.state == 0 && result.outcome == 0 && result.value == 1;
    }
    future.terminal = true;
    if (!allowed) { future.owner.fail(SessionFault::native_profile); result = {0, 1, 1, 0}; }
    // This result completes only provider reading. The unchanged native parent
    // still owns deserialization, cache import, notification and its finalizer.
    *out = result; return out;
}
const SaveFutureVtable vtable{destroy, poll};
}
SaveFuture** profile_read_prerequisite(Session& owner, engine::Memory& memory, uintptr_t provider, SaveFuture** out,
    uintptr_t identity, SaveReference* input, const ProfilePrerequisiteCalls& calls) {
    if (!owner.routed()) return calls.read(provider, out, identity, input);
    auto future = std::unique_ptr<PrerequisiteFuture>(new (std::nothrow) PrerequisiteFuture(owner, calls));
    bool valid = false;
    try {
        valid = future && owner.native_io() &&
            read_campaign_prefix(memory, calls.catalog.image_base, future->campaign);
    } catch (const std::bad_alloc&) {}
    if (valid) {
        // Both buffers use the selected executable's allocator. There is no
        // manager registry insertion and therefore no invented registry cleanup.
        auto* control = static_cast<NativeControl*>(calls.allocate(sizeof(NativeControl)));
        if (control) {
            *control = {1, 1, 0, calls.destroy};
            future->catalog_data.control = reinterpret_cast<uintptr_t>(control);
            void* object = calls.allocate(0x280);
            if (object) {
                calls.construct(object); // Proven constructor includes native Clear().
                control->object = reinterpret_cast<uintptr_t>(object);
                // Match the native enumeration preparation on this private,
                // completely constructed SaveData object, before publication.
                const int32_t enumerating = 1; const uint8_t enabled = 1;
                std::memcpy(static_cast<char*>(object) + 0x278, &enumerating, sizeof(enumerating));
                std::memcpy(static_cast<char*>(object) + 0x27d, &enabled, 1);
                std::memcpy(static_cast<char*>(object) + 0x27e, &enabled, 1);
                SaveReference argument{}; calls.catalog.retain(&argument, &future->catalog_data);
                enumerate_provider(owner, memory, provider, &future->enumeration, identity, &argument,
                    future->campaign.c_str(), calls.catalog);
            }
        }
        valid = future->enumeration != nullptr;
    }
    if (!valid) { owner.fail(SessionFault::native_profile); calls.catalog.release(input); *out = refused_save_future(); return out; }
    // Capture identity and the PROFILE reference while the native caller still
    // owns its borrowed identity argument. Construction does not poll/start the
    // read job: that future is held untouched until catalog reconciliation passes.
    calls.read(provider, &future->profile, identity, input);
    if (!future->profile) { owner.fail(SessionFault::native_profile); *out = refused_save_future(); return out; }
    future->vtable = &vtable; *out = future.release(); return out;
}
} // namespace sentinel::save
