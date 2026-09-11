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
    std::string campaign;
    bool profile_done = false, polled = false, terminal = false;
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
    if (!future.polled) {
        future.polled = true;
        future.owner.profile_step(ProfileStage::first_poll, ProfileStatus::succeeded, "native_lifetime_no_core_deadline");
    }
    bool allowed = false;
    const char* failure = "session_not_live_or_campaign_changed";
    try {
        std::string current;
        allowed = future.owner.native_io() &&
            read_campaign_prefix(future.memory, future.calls.catalog.image_base, current) &&
            steam_name_equal(current, future.campaign);
    } catch (const std::bad_alloc&) {}
    SaveResult result{0, 1, 1, 0};
    if (allowed && !future.profile_done) {
        // Advance the native identity/transport/decode chain first. A pending
        // native future owns its waiter and cancellation; construction time is
        // not an execution budget. Only one child uses the native executor.
        future.profile->vtable->poll(future.profile, &result, executor);
        allowed = result.state == -1 || (result.state == 0 && result.outcome == 0 && result.value == 1);
        failure = "native_profile_transport_result";
        future.owner.profile_step(ProfileStage::transport, !allowed ? ProfileStatus::refused :
            result.state == -1 ? ProfileStatus::pending : ProfileStatus::succeeded, failure,
            true, result.state, result.outcome, result.value);
        if (result.state == -1) { *out = result; return out; }
        future.profile_done = allowed;
        future.profile->vtable->destroy(future.profile, 1); future.profile = nullptr;
    }
    if (allowed) {
        future.owner.profile_step(ProfileStage::catalog_poll, ProfileStatus::succeeded, "after_profile_transport");
        future.enumeration->vtable->poll(future.enumeration, &result, executor);
        if (result.state == -1) { *out = result; return out; }
        allowed = result.state == 0 && result.outcome == 0 && result.value == 1;
        failure = "catalog_prerequisite_result";
        future.enumeration->vtable->destroy(future.enumeration, 1); future.enumeration = nullptr;
        future.calls.catalog.release(&future.catalog_data);
    }
    future.terminal = true;
    if (!allowed) {
        future.owner.profile_step(ProfileStage::request, ProfileStatus::refused, failure);
        future.owner.fail_profile(); result = {0, 1, 1, 0};
    }
    // This result completes only provider reading. The unchanged native parent
    // still owns deserialization, cache import, notification and its finalizer.
    *out = result; return out;
}
const SaveFutureVtable vtable{destroy, poll};
}
SaveFuture** profile_read_prerequisite(Session& owner, engine::Memory& memory, uintptr_t provider, SaveFuture** out,
    uintptr_t identity, SaveReference* input, const ProfilePrerequisiteCalls& calls) {
    if (!owner.routed()) return calls.read(provider, out, identity, input);
    uintptr_t data = 0;
    if (input->control) memory.copy(input->control + 8, &data, sizeof(data));
    owner.begin_profile(data);
    owner.profile_step(ProfileStage::request, ProfileStatus::entered, "native_identity_preserved");
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
    owner.profile_step(ProfileStage::catalog_created, valid ? ProfileStatus::succeeded : ProfileStatus::refused,
        valid ? "native_catalog_future_constructed" : "catalog_construction_or_campaign_refused");
    if (!valid) { owner.fail_profile(); calls.catalog.release(input); *out = refused_save_future(); return out; }
    // Capture identity and the PROFILE reference while the native caller still
    // owns its borrowed identity argument. Construction does not poll/start the
    // read job. Poll advances it before the AP catalog; neither is imported yet.
    calls.read(provider, &future->profile, identity, input);
    owner.profile_step(ProfileStage::profile_created, future->profile ? ProfileStatus::succeeded : ProfileStatus::refused,
        future->profile ? "native_profile_future_constructed" : "null_profile_future", true);
    if (!future->profile) { owner.fail(SessionFault::native_profile); *out = refused_save_future(); return out; }
    future->vtable = &vtable; *out = future.release(); return out;
}
} // namespace sentinel::save
