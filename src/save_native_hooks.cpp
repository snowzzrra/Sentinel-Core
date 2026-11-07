#include "prelaunch.h"
// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_native_hooks.h"
#include "save_startup_gate.h"
#include "save_campaign_native.h"
#include "save_collector.h"
#include "save_delete.h"
#include "save_write.h"
#include "save_profile.h"
#include "save_provider.h"
#include "save_catalog.h"
#include "save_profile_prerequisite.h"
#include "save_readback.h"
#include "native_target.h"
#include "native_runtime.h"
#include "save_hook_call.h"
#include <intrin.h>
#include <cstring>

namespace sentinel::save {
namespace {
MH_STATUS WINAPI enable_root_hook(void*);
MH_STATUS WINAPI disable_root_hook(void*);
hooks::HookSet root_hooks("save_root", {MH_CreateHook, enable_root_hook, disable_root_hook, MH_RemoveHook},
                          &session().installation, 2);
MH_STATUS WINAPI enable_save_hook(void*);
MH_STATUS WINAPI disable_save_hook(void*);
MH_STATUS WINAPI remove_save_hook(void*);
hooks::HookSet save_hooks("save", {MH_CreateHook, enable_save_hook, disable_save_hook, remove_save_hook},
                          &session().installation, 2);
std::array<native::Target, 45> save_targets{};
engine::Binding save_binding{};
uintptr_t save_initializer = 0;
void* save_originals[30]{};
SubmissionCalls submission_calls{};
NativeSaveFactory original_save_factory = nullptr;
SaveReference* save_factory_detour(uintptr_t manager, SaveReference* out, uint32_t user, uintptr_t request,
                                   uintptr_t caller) {
    return campaign_save_factory(caller, submission_calls.factory_caller, manager, out, user, request,
                                 original_save_factory);
}
using RootInit = void (*)(uintptr_t);
RootInit original_root = nullptr;
StartupGate startup_gate;
bool startup_gate_deferred = false;
uintptr_t startup_image=0, startup_expected_root=0, startup_expected_caller=0;
CollectorCalls calls{};
DeleteCalls delete_calls{};
DeleteCalls scoped_delete_calls{};
DeleteOperation original_auxiliary_operation = nullptr;
ProviderCalls provider_calls{};
DeleteOperationResult* auxiliary_operation_detour(uintptr_t context, DeleteOperationResult* out, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return delete_auxiliary_scoped(session(), memory, context, out, original_auxiliary_operation);
}
WriteCalls write_calls{};
WriteCalls read_calls{};
WriteCalls erase_calls{}, auxiliary_calls{};
CreateEnumeration original_enumeration = nullptr;
ProfileCalls profile_calls{};
ExistenceCalls existence_calls{};
CatalogCalls catalog_calls{};
ProfilePrerequisiteCalls prerequisite_calls{};
ReadbackCalls readback_calls{};
PrepareRead original_read_preparation = nullptr;
DecodeRead original_read_decode = nullptr;
ReadWorkerResult* read_preparation_detour(uintptr_t context, ReadWorkerResult* out, SaveReference* waiter) {
    engine::LocalMemory memory;
    return prepare_readback(session(), memory, context, out, waiter, original_read_preparation);
}
ReadWorkerResult* read_decode_detour(uintptr_t context, ReadWorkerResult* out) {
    engine::LocalMemory memory;
    return verify_readback(session(), memory, context, out, original_read_decode, readback_calls.native.catalog.image_base);
}
using InitializeProvider = void (*)(uintptr_t);
InitializeProvider original_provider = nullptr;
InitializeProvider original_provider_reset = nullptr;
using AccountRemoved = void (*)(uintptr_t, uintptr_t, uintptr_t);
AccountRemoved original_account_removed = nullptr;
void observe_provider_invalidation(uintptr_t manager, uintptr_t caller, uint32_t origin) {
    void* frames[32]{};
    const auto count = RtlCaptureStackBackTrace(1,32,frames,nullptr);
    engine::LocalMemory memory;
    invalidate_provider(session(),memory,manager,provider_calls.image_base,caller,origin,
        reinterpret_cast<const uintptr_t*>(frames),count);
}
void account_removed_detour(uintptr_t callback, uintptr_t manager, uintptr_t user, uintptr_t caller) {
    // A removed native account may later reuse an identity address. Refuse the
    // transition before the manager releases its cached references, even if the
    // process-wide Steam RemoteStorage singleton has not changed address.
    observe_provider_invalidation(manager, caller, 2);
    original_account_removed(callback, manager, user);
}
void provider_reset_detour(uintptr_t manager, uintptr_t caller) {
    observe_provider_invalidation(manager, caller, 1);
    original_provider_reset(manager);
}
WritePreflight original_preflight = nullptr;
SdkWriteCalls sdk_calls{};
SdkWriteCallback original_sdk_callback = nullptr;
DestroySdkVector original_sdk_vector = nullptr;
PrepareWriteJob original_prepare_job = nullptr;
CreateWriteContext original_write_context = nullptr;
DestroySdkVector original_destroy_preparation = nullptr, original_destroy_preflight = nullptr;
uintptr_t* prepare_job_detour(const SaveReference* source, uintptr_t* out, const SaveReference* identity,
                              uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return prepare_write_job(session(), memory, source, out, identity, original_prepare_job, sdk_calls.image_base);
}
SaveFuture** write_context_detour(SaveFuture** out, SaveReference* identity, const char* name, uint8_t clear,
                                  uint64_t* files, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return create_write_context(session(), memory, out, identity, name, clear, files, original_write_context, sdk_calls.image_base);
}
void destroy_preparation_detour(uintptr_t object) { destroy_write_job(session(), object, original_destroy_preparation); }
void destroy_preflight_detour(uintptr_t object) { destroy_write_job(session(), object, original_destroy_preflight); }
SdkWriteResult* sdk_poll_detour(uintptr_t context, SdkWriteResult* out, void* executor, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return poll_sdk_write(session(), memory, context, out, executor, sdk_calls);
}
void sdk_callback_detour(uintptr_t callback, const int32_t* result, bool failed) {
    engine::LocalMemory memory;
    sdk_write_callback(session(), memory, callback, result, failed, original_sdk_callback);
}
void sdk_vector_detour(uintptr_t vector) {
    engine::LocalMemory memory;
    destroy_sdk_vector(session(), memory, vector, original_sdk_vector);
}
PrepareProfile original_prepare_profile = nullptr;
using ClearSaveData = void (*)(uintptr_t);
ClearSaveData original_clear_data = nullptr, original_destroy_data = nullptr;
void clear_data_detour(uintptr_t data) {
    session().forget_save_data(data); original_clear_data(data);
}
void destroy_data_detour(uintptr_t data) {
    session().forget_save_data(data); original_destroy_data(data);
}
void prepare_profile_detour(SaveReference* profile, SaveReference* data, uintptr_t native_user, const char* suffix,
                            uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    prepare_profile_write(session(), memory, profile, data, native_user, suffix, original_prepare_profile,
        catalog_calls.retain, profile_calls);
}
WritePreflightResult* preflight_detour(uintptr_t context, WritePreflightResult* out, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return preflight_scoped(session(), memory, context, out, original_preflight, sdk_calls.image_base);
}
std::atomic<bool> owner_pinned{false};
SaveFuture** existence_detour(SaveFuture** out, SaveReference* identity, const char* name, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return query_exists_scoped(session(), memory, out, identity, name, existence_calls);
}
void root_provider_ready() {
    engine::LocalMemory memory;
    bind_root_provider(session(), memory, provider_calls);
}
void invoke_root(uintptr_t root, bool entered, RootInit original) {
    __try {
        original(root);
        if (entered) root_provider_ready();
    } __finally {
        if (entered) session().startup_leave(AbnormalTermination() != FALSE);
    }
}
void root_detour(uintptr_t root, uintptr_t caller, RootInit original) {
    NativeRouteScope route(caller,startup_image);
    startup_gate.entered(root==startup_expected_root,caller==startup_expected_caller,
        caller>=startup_image?caller-startup_image:0);
    startup_gate.record(session().btrace,BStatus::pending,"startup_gate_root_waiting",StartupGate::State::pending);
    const auto result=startup_gate.wait();
    const bool ready=result.state==StartupGate::State::ready;
    startup_gate.record(session().btrace,ready?BStatus::succeeded:BStatus::refused,
        ready?"startup_gate_root_released":"startup_gate_root_installation_unavailable",result.state,result.error);
    if (!ready) session().reject(SessionFault::installation);
    const bool entered=ready && session().startup_enter(root,caller,GetCurrentThreadId());
    // RootInit must initialize the native manager even after startup refusal.
    invoke_root(root, entered, original);
}
void provider_detour(uintptr_t manager) {
    original_provider(manager);
    engine::LocalMemory memory;
    provider_initialized(session(), memory, manager, provider_calls);
}
CollectorResult* collector_detour(const CollectorContext* context, CollectorResult* out, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return collect_scoped(session(), memory, context, out, calls);
}
DeleteResult* delete_detour(DeleteFuture* future, DeleteResult* out, void* executor, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return refuse_unscoped_delete(session(), memory, future, out, executor, delete_calls);
}
DeleteResult* scoped_delete_detour(DeleteFuture* future, DeleteResult* out, void* executor, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return poll_scoped_delete(session(), memory, future, out, executor, scoped_delete_calls);
}
SaveFuture** write_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data,
                          uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return campaign_write_provider(caller, memory, provider, out, identity, data, write_calls);
}
SaveFuture** read_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data,
                         uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return read_scoped(session(), memory, provider, out, identity, data, read_calls);
}
SaveFuture** catalog_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data,
                            const char* prefix, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return enumerate_provider(session(), memory, provider, out, identity, data, prefix, catalog_calls);
}
SaveFuture** prerequisite_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    engine::LocalMemory memory;
    return profile_read_prerequisite(session(), memory, provider, out, identity, data, prerequisite_calls);
}
SaveFuture** erase_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data,
                          uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return delete_scoped(session(), memory, provider, out, identity, data, erase_calls, false);
}
SaveFuture** auxiliary_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data,
                              uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return delete_scoped(session(), memory, provider, out, identity, data, auxiliary_calls, true);
}
EnumerationFuture** enumeration_detour(EnumerationFuture** out, SaveReference* identity, const char* root,
                                       const char* prefix, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return enumerate_scoped(session(), memory, out, identity, root, prefix, original_enumeration);
}
uint64_t profile_read_detour(SaveReference* profile, SaveReference* data, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return read_profile(session(), memory, profile, data, profile_calls);
}
uint32_t profile_serialize_detour(uintptr_t manager, uintptr_t profile, ProfileHolder* holder, uintptr_t caller) {
    NativeRouteScope route(caller, provider_calls.image_base);
    engine::LocalMemory memory;
    return serialize_profile(session(), memory, manager, profile, holder, profile_calls);
}
bool profile_payload(Session& owner, engine::Memory& memory, uintptr_t data) {
    return profile_payload_valid(owner, memory, data, profile_calls);
}
void root_detour_entry(uintptr_t root) {
    const auto lease = root_hooks.enter(0);
    __try {
        root_detour(root, reinterpret_cast<uintptr_t>(_ReturnAddress()), reinterpret_cast<RootInit>(lease.original));
    } __finally {
        root_hooks.leave(lease);
    }
}

CollectorResult* collector_detour_entry(const CollectorContext* context, CollectorResult* out) {
    const auto lease = save_hooks.enter(0);
    __try {
        return collector_detour(context, out, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

DeleteResult* delete_detour_entry(DeleteFuture* future, DeleteResult* out, void* executor) {
    const auto lease = save_hooks.enter(1);
    __try {
        return delete_detour(future, out, executor, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

SaveFuture** write_detour_entry(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    const auto lease = save_hooks.enter(2);
    __try {
        return write_detour(provider, out, identity, data, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

uint64_t profile_read_detour_entry(SaveReference* profile, SaveReference* data) {
    const auto lease = save_hooks.enter(3);
    __try {
        return profile_read_detour(profile, data, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

uint32_t profile_serialize_detour_entry(uintptr_t manager, uintptr_t profile, ProfileHolder* holder) {
    const auto lease = save_hooks.enter(4);
    __try {
        return profile_serialize_detour(manager, profile, holder, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

SaveFuture** read_detour_entry(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    const auto lease = save_hooks.enter(5);
    __try {
        return read_detour(provider, out, identity, data, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

EnumerationFuture** enumeration_detour_entry(EnumerationFuture** out, SaveReference* identity, const char* root,
                                             const char* prefix) {
    const auto lease = save_hooks.enter(6);
    __try {
        return enumeration_detour(out, identity, root, prefix, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

SaveFuture** erase_detour_entry(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    const auto lease = save_hooks.enter(7);
    __try {
        return erase_detour(provider, out, identity, data, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

SaveFuture** auxiliary_detour_entry(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    const auto lease = save_hooks.enter(8);
    __try {
        return auxiliary_detour(provider, out, identity, data, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

void provider_detour_entry(uintptr_t manager) {
    const auto lease = save_hooks.enter(9);
    __try {
        return provider_detour(manager);
    } __finally {
        save_hooks.leave(lease);
    }
}

SaveFuture** catalog_detour_entry(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data,
                                  const char* prefix) {
    const auto lease = save_hooks.enter(10);
    __try {
        return catalog_detour(provider, out, identity, data, prefix, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

WritePreflightResult* preflight_detour_entry(uintptr_t context, WritePreflightResult* out) {
    const auto lease = save_hooks.enter(11);
    __try {
        return preflight_detour(context, out, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

void prepare_profile_detour_entry(SaveReference* profile, SaveReference* data, uintptr_t native_user,
                                  const char* suffix) {
    const auto lease = save_hooks.enter(12);
    __try {
        return prepare_profile_detour(profile, data, native_user, suffix,
                                      reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

void clear_data_detour_entry(uintptr_t data) {
    const auto lease = save_hooks.enter(13);
    __try {
        return clear_data_detour(data);
    } __finally {
        save_hooks.leave(lease);
    }
}

void destroy_data_detour_entry(uintptr_t data) {
    const auto lease = save_hooks.enter(14);
    __try {
        return destroy_data_detour(data);
    } __finally {
        save_hooks.leave(lease);
    }
}

SaveFuture** existence_detour_entry(SaveFuture** out, SaveReference* identity, const char* name) {
    const auto lease = save_hooks.enter(15);
    __try {
        return existence_detour(out, identity, name, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

SdkWriteResult* sdk_poll_detour_entry(uintptr_t context, SdkWriteResult* out, void* executor) {
    const auto lease = save_hooks.enter(16);
    __try {
        return sdk_poll_detour(context, out, executor, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

void sdk_callback_detour_entry(uintptr_t callback, const int32_t* result, bool failed) {
    const auto lease = save_hooks.enter(17);
    __try {
        return sdk_callback_detour(callback, result, failed);
    } __finally {
        save_hooks.leave(lease);
    }
}

void sdk_vector_detour_entry(uintptr_t vector) {
    const auto lease = save_hooks.enter(18);
    __try {
        return sdk_vector_detour(vector);
    } __finally {
        save_hooks.leave(lease);
    }
}

uintptr_t* prepare_job_detour_entry(const SaveReference* source, uintptr_t* out, const SaveReference* identity) {
    const auto lease = save_hooks.enter(19);
    __try {
        return prepare_job_detour(source, out, identity, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

void destroy_preparation_detour_entry(uintptr_t object) {
    const auto lease = save_hooks.enter(20);
    __try {
        return destroy_preparation_detour(object);
    } __finally {
        save_hooks.leave(lease);
    }
}

SaveFuture** write_context_detour_entry(SaveFuture** out, SaveReference* identity, const char* name, uint8_t clear,
                                        uint64_t* files) {
    const auto lease = save_hooks.enter(21);
    __try {
        return write_context_detour(out, identity, name, clear, files, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

void destroy_preflight_detour_entry(uintptr_t object) {
    const auto lease = save_hooks.enter(22);
    __try {
        return destroy_preflight_detour(object);
    } __finally {
        save_hooks.leave(lease);
    }
}

ReadWorkerResult* read_preparation_detour_entry(uintptr_t context, ReadWorkerResult* out, SaveReference* waiter) {
    const auto lease = save_hooks.enter(23);
    __try {
        return read_preparation_detour(context, out, waiter);
    } __finally {
        save_hooks.leave(lease);
    }
}

ReadWorkerResult* read_decode_detour_entry(uintptr_t context, ReadWorkerResult* out) {
    const auto lease = save_hooks.enter(24);
    __try {
        return read_decode_detour(context, out);
    } __finally {
        save_hooks.leave(lease);
    }
}

SaveReference* save_factory_detour_entry(uintptr_t manager, SaveReference* out, uint32_t user, uintptr_t request) {
    const auto lease = save_hooks.enter(25);
    __try {
        return save_factory_detour(manager, out, user, request, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

DeleteResult* scoped_delete_detour_entry(DeleteFuture* future, DeleteResult* out, void* executor) {
    const auto lease = save_hooks.enter(26);
    __try {
        return scoped_delete_detour(future, out, executor, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

DeleteOperationResult* auxiliary_operation_detour_entry(uintptr_t context, DeleteOperationResult* out) {
    const auto lease = save_hooks.enter(27);
    __try {
        return auxiliary_operation_detour(context, out, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

void provider_reset_detour_entry(uintptr_t manager) {
    const auto lease = save_hooks.enter(28);
    __try {
        return provider_reset_detour(manager, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}

void account_removed_detour_entry(uintptr_t callback, uintptr_t manager, uintptr_t user) {
    const auto lease = save_hooks.enter(29);
    __try {
        return account_removed_detour(callback, manager, user, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    } __finally {
        save_hooks.leave(lease);
    }
}
bool checked_bytes(engine::Memory& memory, uintptr_t address, const uint8_t* expected, size_t count, sc_install_event& event) {
#ifdef SC_NATIVE_TESTING
    if (!testing::real_hook_targets()) return true;
#endif
    std::array<uint8_t, 88> actual{};
    const auto read = memory.copy(address, actual.data(), count);
    attach_read(event, read);
    size_t mismatch = 0;
    if (!read.reason) while (mismatch < count && actual[mismatch] == expected[mismatch]) ++mismatch;
    const size_t window = mismatch < count ? (mismatch / 32) * 32 : 0;
    event.byte_window_offset = static_cast<uint32_t>(window);
    event.byte_count = static_cast<uint32_t>(std::min(count - window, size_t{32}));
    std::memcpy(event.expected_bytes, expected + window, event.byte_count);
    std::memcpy(event.actual_bytes, actual.data() + window, event.byte_count);
    return !read.reason && !std::memcmp(actual.data(), expected, count);
}
bool reference_copy_entry(engine::Memory& memory, const engine::Image& image, sc_install_event& event) {
    event.reason = SC_NATIVE_TARGET_BOUNDARY;
    // This leaf has no .pdata entry. A CALL in the separately validated native
    // enum finalizer establishes its entry; all 88 body bytes are fixed as well.
    constexpr char hex[] = "48c701000000004c8bc1488b124885d274428b420485c074360f1f80000000008d4801f00fb14a0474108b420485c075ef33d2498bc0498910c38b0285c0740f8d4801f00fb10a74088b0285c075f133d2498910498bc0c3";
    bool executable = false;
    for (const auto& section : image.sections) {
        if (0x367510 >= section.rva && uint64_t{0x367510 + 88} <= uint64_t{section.rva} + section.size &&
            (section.flags & (IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE)) == (IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE) &&
            !(section.flags & IMAGE_SCN_MEM_WRITE)) executable = true;
    }
    if (!executable) return false;
    native::ValidationDetail detail;
    if (!native::function_window(memory, image, image.base + 0x148dd10, image.base + 0x148de8c, 5, &detail)) {
        if (detail.read_attempted) attach_read(event, detail.read);
        return false;
    }
    constexpr std::array<uint8_t, 5> expected_call{0xe8, 0x7f, 0x96, 0xed, 0xfe};
    event.reason = SC_NATIVE_TARGET_BYTES;
    event.rva = 0x148de8c;
    if (!checked_bytes(memory, image.base + event.rva, expected_call.data(), expected_call.size(), event)) return false;
    std::array<uint8_t, 88> body{};
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>(digit(hex[2 * i]) * 16 + digit(hex[2 * i + 1]));
    event.rva = 0x367510;
    return checked_bytes(memory, image.base + event.rva, body.data(), body.size(), event);
}
void configure_save_calls() {
    const auto& binding = save_binding;
    const auto& targets = save_targets;
    const auto context_init = reinterpret_cast<InitializeSteamContext>(save_initializer);
    original_provider = HookOriginal<save_hooks, 9, InitializeProvider>::call;
    original_preflight = HookOriginal<save_hooks, 11, WritePreflight>::call;
    original_prepare_profile = HookOriginal<save_hooks, 12, PrepareProfile>::call;
    original_clear_data = HookOriginal<save_hooks, 13, ClearSaveData>::call;
    original_destroy_data = HookOriginal<save_hooks, 14, ClearSaveData>::call;
    provider_calls = {binding.image.base, context_init};
    sdk_calls = {HookOriginal<save_hooks, 16, PollSdkWrite>::call, binding.image.base, context_init};
    original_sdk_callback = HookOriginal<save_hooks, 17, SdkWriteCallback>::call;
    original_sdk_vector = HookOriginal<save_hooks, 18, DestroySdkVector>::call;
    original_prepare_job = HookOriginal<save_hooks, 19, PrepareWriteJob>::call;
    original_destroy_preparation = HookOriginal<save_hooks, 20, DestroySdkVector>::call;
    original_write_context = HookOriginal<save_hooks, 21, CreateWriteContext>::call;
    original_destroy_preflight = HookOriginal<save_hooks, 22, DestroySdkVector>::call;
    original_read_preparation = HookOriginal<save_hooks, 23, PrepareRead>::call;
    original_read_decode = HookOriginal<save_hooks, 24, DecodeRead>::call;
    original_save_factory = HookOriginal<save_hooks, 25, NativeSaveFactory>::call;
    existence_calls = {provider_calls, HookOriginal<save_hooks, 15, QueryExists>::call,
                       reinterpret_cast<ReleaseSaveReference>(targets[5].address)};
    calls = {HookOriginal<save_hooks, 0, Collect>::call, reinterpret_cast<AssignString>(targets[2].address),
             reinterpret_cast<ReleaseVector>(targets[3].address)};
    delete_calls = {HookOriginal<save_hooks, 1, PollDelete>::call, reinterpret_cast<ReleaseDelete>(targets[5].address)};
    scoped_delete_calls = {HookOriginal<save_hooks, 26, PollDelete>::call, delete_calls.release};
    original_auxiliary_operation = HookOriginal<save_hooks, 27, DeleteOperation>::call;
    original_provider_reset = HookOriginal<save_hooks, 28, InitializeProvider>::call;
    original_account_removed = HookOriginal<save_hooks, 29, AccountRemoved>::call;
    write_calls = {HookOriginal<save_hooks, 2, CreateWrite>::call, reinterpret_cast<SetSaveName>(targets[7].address),
                   reinterpret_cast<ReleaseSaveReference>(targets[5].address), profile_payload, binding.image.base};
    submission_calls = {reinterpret_cast<NativeSave>(targets[39].address), write_calls.release, binding.root,
                        binding.image.base + 0x674744};
    read_calls = {HookOriginal<save_hooks, 5, CreateWrite>::call, write_calls.set_name, write_calls.release, nullptr,
                  binding.image.base};
    catalog_calls = {HookOriginal<save_hooks, 10, EnumerateProvider>::call,
                     reinterpret_cast<RetainSaveReference>(binding.image.base + 0x367510),
                     write_calls.release,
                     write_calls.set_name,
                     calls.assign,
                     binding.image.base};
    prerequisite_calls = {reinterpret_cast<AllocateNative>(targets[20].address),
                          reinterpret_cast<ConstructSaveData>(targets[21].address),
                          reinterpret_cast<DestroySaveData>(targets[22].address), catalog_calls, read_calls.create};
    readback_calls = {prerequisite_calls,
                      reinterpret_cast<decltype(ReadbackCalls::construct_stream)>(targets[36].address)};
    write_calls.readback = &readback_calls;
    read_calls.profile_read = prerequisite_detour;
    original_enumeration = HookOriginal<save_hooks, 6, CreateEnumeration>::call;
    erase_calls = {HookOriginal<save_hooks, 7, CreateWrite>::call, write_calls.set_name, write_calls.release, nullptr,
                   binding.image.base};
    auxiliary_calls = {HookOriginal<save_hooks, 8, CreateWrite>::call, write_calls.set_name, write_calls.release,
                       nullptr, binding.image.base};
    profile_calls = {HookOriginal<save_hooks, 3, ReadProfile>::call,
                     HookOriginal<save_hooks, 4, SerializeProfile>::call,
                     reinterpret_cast<LookupProfileValue>(targets[10].address),
                     reinterpret_cast<DestroyProfileValue>(targets[11].address),
                     reinterpret_cast<ProfileChecksum>(targets[12].address),
                     reinterpret_cast<ReleaseSaveReference>(targets[5].address),
                     binding.image.base};
}
MH_STATUS WINAPI enable_save_hook(void* site) {
    engine::LocalMemory memory;
    const auto address = reinterpret_cast<uintptr_t>(site);
    const auto target =
        std::find_if(save_targets.begin(), save_targets.end(), [&](const auto& t) { return t.address == address; });
    auto event =
        session().installation.begin(SC_INSTALL_SAVE_ENABLE, 2, static_cast<uint32_t>(target - save_targets.begin()),
                                     static_cast<uint32_t>(address - save_binding.image.base));
    if (!checked_bytes(memory, address, target->bytes.data(), 32, event)) {
        session().installation.finish(event, SC_NATIVE_TARGET_BYTES);
        return MH_ERROR_UNSUPPORTED_FUNCTION;
    }
    session().installation.finish(event);
    return MH_EnableHook(site);
}
MH_STATUS WINAPI enable_root_hook(void* site) {
    engine::LocalMemory memory;
    auto event = session().installation.begin(SC_INSTALL_SAVE_ENABLE, 2, 0,
                                              static_cast<uint32_t>(save_targets[0].address - save_binding.image.base));
    if (!checked_bytes(memory, save_targets[0].address, save_targets[0].bytes.data(), 32, event)) {
        session().installation.finish(event, SC_NATIVE_TARGET_BYTES);
        return MH_ERROR_UNSUPPORTED_FUNCTION;
    }
    session().installation.finish(event);
    return MH_EnableHook(site);
}
MH_STATUS WINAPI disable_root_hook(void* site) {
    session().reject(SessionFault::installation);
    {
        StartupGate::Completion completion(startup_gate, &session().btrace);
    }
    return MH_DisableHook(site);
}
MH_STATUS WINAPI disable_save_hook(void* site) {
    return drain_native_startup() ? MH_DisableHook(site) : MH_ERROR_MEMORY_PROTECT;
}
MH_STATUS WINAPI remove_save_hook(void* site) {
    return drain_native_startup() ? MH_RemoveHook(site) : MH_ERROR_MEMORY_PROTECT;
}
}
void configure_prelaunch() {
    if (session().configured()) return;
    auto& record = session().installation;
    record.initialize();
    auto event = record.begin(SC_INSTALL_PRELAUNCH);
    storage::Descriptor descriptor;
    bool requested = false;
    auto result = prelaunch::resolve(descriptor, requested);
    if (!requested && result.ok()) { record.finish(event); return; }
    std::unique_ptr<storage::Namespace> lease;
    if (result.ok()) result = storage::reopen(descriptor, lease);
    record.finish(event, result.ok() ? SC_NATIVE_NONE : SC_NATIVE_BINDING_FAILED, SC_INSTALL_UNKNOWN, result.win32_error);
    if (result.ok()) {
        // the one-use descriptor/lease owner must survive even when startup is missed or binding fails before any native hook becomes reachable
        HMODULE module = nullptr;
        event = record.begin(SC_INSTALL_PIN);
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&configure_prelaunch), &module)) {
            const auto error = GetLastError();
            record.finish(event, SC_NATIVE_PIN_FAILED, SC_INSTALL_UNKNOWN, error);
            session().reject(SessionFault::installation); return;
        }
        record.finish(event);
        owner_pinned.store(true, std::memory_order_release);
    }
    if (result.ok()) result = session().configure(descriptor, std::move(lease));
    if (!result.ok()) session().reject(SessionFault::descriptor);
}
bool owner_retained() { return owner_pinned.load(std::memory_order_acquire); }
static bool validate_profile_factory_calls(Installation& record, engine::Memory& memory, const engine::Image& image) {
    constexpr std::array<uint8_t, 22> checkpoint_call{
        0x49,0x8b,0x02,0x4c,0x8d,0x4c,0x24,0x38,0x4d,0x8b,0xc7,
        0x48,0x8d,0x54,0x24,0x58,0x49,0x8b,0xca,0xff,0x50,0x18};
    auto event = record.begin(SC_INSTALL_FACTORY_CALL, 3, SC_INSTALL_UNKNOWN, 0x14897ad);
    const bool checkpoint_ok = image.contains(event.rva, checkpoint_call.size(),
        IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE) &&
        checked_bytes(memory, image.base + event.rva, checkpoint_call.data(), checkpoint_call.size(), event);
    record.finish(event, checkpoint_ok ? SC_NATIVE_NONE : SC_NATIVE_TARGET_BYTES);
    if (!checkpoint_ok) return false;
    // only this checked native callback may delegate the shared profile presence query before routing
    constexpr std::array<uint8_t,5> presence_call{0xe8,0x26,0x2f,0xff,0xff};
    event=record.begin(SC_INSTALL_FACTORY_CALL,3,SC_INSTALL_UNKNOWN,0x1be4b35);
    const bool presence_ok=image.contains(event.rva,5,IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_EXECUTE,IMAGE_SCN_MEM_WRITE) &&
        checked_bytes(memory,image.base+event.rva,presence_call.data(),presence_call.size(),event);
    record.finish(event,presence_ok?SC_NATIVE_NONE:SC_NATIVE_TARGET_BYTES);
    if (!presence_ok) return false;
    return true;
}
bool validate_native_helpers(Installation& record, engine::Memory& memory, const engine::Image& image, uintptr_t& initializer) {
    auto event = record.begin(SC_INSTALL_REFERENCE_COPY, 3, SC_INSTALL_UNKNOWN, 0x367510);
    const bool reference_ok = reference_copy_entry(memory, image, event);
    record.finish(event, reference_ok ? SC_NATIVE_NONE : event.reason);
    if (!reference_ok) { return false; }
    constexpr std::array<uint8_t, 5> expected_factory_call{0xe8, 0x3c, 0x13, 0xe2, 0x00};
    event = record.begin(SC_INSTALL_FACTORY_CALL, 3, SC_INSTALL_UNKNOWN, 0x67473f);
    const bool factory_scope = image.contains(event.rva, 5, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE);
    const bool factory_ok = factory_scope &&
        checked_bytes(memory, image.base + event.rva, expected_factory_call.data(), expected_factory_call.size(), event);
    record.finish(event, factory_ok ? SC_NATIVE_NONE : (factory_scope ? SC_NATIVE_TARGET_BYTES : SC_NATIVE_TARGET_BOUNDARY));
    if (!factory_ok) { return false; }
    if (!validate_profile_factory_calls(record, memory, image)) return false;
    InitializeSteamContext context_init = nullptr;
    event = record.begin(SC_INSTALL_STEAM_IMPORT, 3, SC_INSTALL_UNKNOWN, 0x2a1cc60);
    const auto import_read = memory.copy(image.base + event.rva, &context_init, sizeof(context_init));
    attach_read(event, import_read);
    record.finish(event, import_read.reason ? SC_NATIVE_READ_FAILED : (!context_init ? SC_NATIVE_BINDING_FAILED : SC_NATIVE_NONE));
    if (import_read.reason || !context_init) {
        return false;
    }
    initializer = reinterpret_cast<uintptr_t>(context_init);
    return true;
}
static StartupGate::Arm observe_startup_arm(engine::Memory& memory, const engine::Binding& binding, const native::Target& root_target) {
    StartupGate::Arm arm{};
    arm.at_ms = GetTickCount64();
    uintptr_t manager=0,dispatch_root=0,dispatch_vtable=0,dispatch_init=0;
    const auto manager_read=memory.copy(binding.root+0x9b38,&manager,sizeof(manager));
    arm.manager_read_reason=manager_read.reason; arm.manager_present=!manager_read.reason && manager!=0;
    const auto root_read=memory.copy(binding.image.base+0x38913a0,&dispatch_root,sizeof(dispatch_root));
    arm.root_read_reason=root_read.reason; arm.root_equals_binding=!root_read.reason && dispatch_root==binding.root;
    arm.vtable_read_reason=arm.init_read_reason=root_read.reason?SC_REASON_PARENT_UNAVAILABLE:SC_REASON_PARENT_NULL;
    if (!root_read.reason && dispatch_root) {
        const auto vtable_read=memory.copy(dispatch_root,&dispatch_vtable,sizeof(dispatch_vtable));
        arm.vtable_read_reason=vtable_read.reason;
        arm.init_read_reason=vtable_read.reason?SC_REASON_PARENT_UNAVAILABLE:SC_REASON_PARENT_NULL;
        if (!vtable_read.reason && dispatch_vtable) {
            const auto init_read=memory.copy(dispatch_vtable+0x18,&dispatch_init,sizeof(dispatch_init));
            arm.init_read_reason=init_read.reason; arm.init_equals_target=!init_read.reason && dispatch_init==root_target.address;
        }
    }
    return arm;
}
static bool save_install_cancelled(HANDLE stop, StartupGate::Completion& gate_completion) {
        if (!stop || WaitForSingleObject(stop,0)==WAIT_TIMEOUT) return false;
        gate_completion.cancel(); return true;
}
static bool publish_save_policy(const engine::Binding& binding, const hooks::HookSpec (&specs)[std::size(save_originals)], HANDLE stop, StartupGate::Completion& gate_completion) {
    auto& record = session().installation;
    configure_save_calls();
    auto event = record.begin(SC_INSTALL_POLICY);
    session().install(binding.root, binding.image.base + 0x4323fc, steam_20260818_routes);
    const bool published = session().inspect().prepared_routes == steam_20260818_routes;
    record.finish(event, published ? SC_NATIVE_NONE : SC_NATIVE_CANCELLED);
    if (!published || !save_hooks.install(specs, stop) || !install_campaign_hooks(binding, stop) || save_install_cancelled(stop, gate_completion)) {
        session().reject(SessionFault::installation);
        return false;
    }
    return true;
}
static bool validate_remaining_save_targets(Installation& record, engine::LocalMemory& memory, const engine::Binding& binding, HANDLE stop, StartupGate::Completion& gate_completion) {
    auto& targets = save_targets;
    for (unsigned i = 1; i < targets.size(); ++i) {
        targets[i] = native::save_target(binding.image.base, i);
        if (native::validate_recorded(record, memory, binding.image, targets[i], stop, GetTickCount64() + 3000, 2, i)) {
            save_install_cancelled(stop, gate_completion); session().reject(SessionFault::installation); return false;
        }
    }
    return true;
}
void install_native_hooks(const engine::Binding& binding, HANDLE stop) {
    if (session().state() != SessionState::prepared) return;
    auto& record = session().installation;
    auto event = record.begin(SC_INSTALL_SAVE_BINDING);
    if (binding.metadata.profile != SC_PROFILE_STEAM_20260818 ||
        binding.root != binding.image.base + 0x45ea6f0) {
        record.finish(event, SC_NATIVE_BINDING_FAILED);
        session().reject(SessionFault::installation); return;
    }
    record.finish(event);
    engine::LocalMemory memory;
    auto& targets = save_targets;
    save_binding = binding;
    if (!startup_gate.arm()) {
        session().btrace.record(BStage::startup_gate,BStatus::refused,"startup_gate_event_creation_failed",0,
            {{"win32",GetLastError()}});
        session().reject(SessionFault::installation); return;
    }
    hooks::Transaction attempt;
    StartupGate::Completion gate_completion(startup_gate,&session().btrace);
    startup_image=binding.image.base; startup_expected_root=binding.root;
    startup_expected_caller=binding.image.base+0x4323fc;
    targets[0]=native::save_target(binding.image.base,0);
    if (native::validate_recorded(record,memory,binding.image,targets[0],stop,GetTickCount64()+3000,2,0)) {
        save_install_cancelled(stop, gate_completion); session().reject(SessionFault::installation); return;
    }
    const auto arm = observe_startup_arm(memory, binding, targets[0]);
    startup_gate.observe_arm(arm);
    // Presence is evidence only: neither a null manager nor a matching dispatch
    // can retrospectively qualify a native startup that already passed.
    startup_gate.record(session().btrace,BStatus::pending,arm.manager_present?
        "startup_gate_manager_already_published":"startup_gate_arming",StartupGate::State::pending);
    const hooks::HookSpec root_spec[]{hooks::bind(targets[0].address, root_detour_entry, original_root)};
    if (save_install_cancelled(stop, gate_completion) || !root_hooks.install(root_spec, stop)) {
        session().reject(SessionFault::installation); return;
    }
    if (!validate_remaining_save_targets(record, memory, binding, stop, gate_completion)) return;
    save_initializer = 0;
#ifdef SC_NATIVE_TESTING
    if (testing::real_hook_targets())
#endif
        if (!validate_native_helpers(record, memory, binding.image, save_initializer)) {
            session().reject(SessionFault::installation);
            return;
        }
    constexpr unsigned hooked[] = {1,  4,  6,  8,  9,  13, 14, 15, 16, 17, 18, 23, 24, 25, 26,
                                   27, 28, 29, 30, 32, 33, 34, 35, 37, 38, 40, 41, 42, 43, 44};
    void* detours[] = {reinterpret_cast<void*>(collector_detour_entry),
                       reinterpret_cast<void*>(delete_detour_entry),
                       reinterpret_cast<void*>(write_detour_entry),
                       reinterpret_cast<void*>(profile_read_detour_entry),
                       reinterpret_cast<void*>(profile_serialize_detour_entry),
                       reinterpret_cast<void*>(read_detour_entry),
                       reinterpret_cast<void*>(enumeration_detour_entry),
                       reinterpret_cast<void*>(erase_detour_entry),
                       reinterpret_cast<void*>(auxiliary_detour_entry),
                       reinterpret_cast<void*>(provider_detour_entry),
                       reinterpret_cast<void*>(catalog_detour_entry),
                       reinterpret_cast<void*>(preflight_detour_entry),
                       reinterpret_cast<void*>(prepare_profile_detour_entry),
                       reinterpret_cast<void*>(clear_data_detour_entry),
                       reinterpret_cast<void*>(destroy_data_detour_entry),
                       reinterpret_cast<void*>(existence_detour_entry),
                       reinterpret_cast<void*>(sdk_poll_detour_entry),
                       reinterpret_cast<void*>(sdk_callback_detour_entry),
                       reinterpret_cast<void*>(sdk_vector_detour_entry),
                       reinterpret_cast<void*>(prepare_job_detour_entry),
                       reinterpret_cast<void*>(destroy_preparation_detour_entry),
                       reinterpret_cast<void*>(write_context_detour_entry),
                       reinterpret_cast<void*>(destroy_preflight_detour_entry),
                       reinterpret_cast<void*>(read_preparation_detour_entry),
                       reinterpret_cast<void*>(read_decode_detour_entry),
                       reinterpret_cast<void*>(save_factory_detour_entry),
                       reinterpret_cast<void*>(scoped_delete_detour_entry),
                       reinterpret_cast<void*>(auxiliary_operation_detour_entry),
                       reinterpret_cast<void*>(provider_reset_detour_entry),
                       reinterpret_cast<void*>(account_removed_detour_entry)};
    hooks::HookSpec specs[std::size(hooked)]{};
    static_assert(std::size(detours) == std::size(hooked) && std::size(save_originals) == std::size(hooked));
    for (size_t i = 0; i < std::size(hooked); ++i) {
        const auto index = hooked[i];
        specs[i] = {targets[index].address, detours[i], &save_originals[i], index,
                    static_cast<uint32_t>(targets[index].address - binding.image.base)};
    }
    if (!publish_save_policy(binding, specs, stop, gate_completion)) return;
    startup_gate_deferred = true;
    attempt.commit();
    gate_completion.disarm();
}
bool complete_native_installation(bool native_ready, HANDLE stop) {
    if (!startup_gate_deferred) {
        if (!native_ready && session().campaign_requested()) session().reject(SessionFault::installation);
        return native_ready && (!session().campaign_requested() || session().state() == SessionState::prepared);
    }
    startup_gate_deferred = false;
    {
        StartupGate::Completion gate_completion(startup_gate, &session().btrace);
        if (stop && WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) {
            gate_completion.cancel();
            session().reject(SessionFault::installation);
            return false;
        }
        if (!native_ready || session().state() != SessionState::prepared) {
            session().reject(SessionFault::installation);
            return false;
        }
        auto& record = session().installation;
        record.finish(record.begin(SC_INSTALL_READY));
        gate_completion.ready();
    }
    const bool ready = startup_gate.wait().state == StartupGate::State::ready;
    if (!ready) session().reject(SessionFault::installation);
    return ready;
}
bool drain_native_startup() {
    if (root_hooks.inspect().state == hooks::State::empty) return true;
    session().reject(SessionFault::installation);
    {
        StartupGate::Completion completion(startup_gate, &session().btrace);
    }
    // RootInit must finish with the lower save guards still reachable.
    return root_hooks.uninstall();
}
#ifdef SC_NATIVE_TESTING
uint32_t test_startup_waiting() { return startup_gate.waiting(); }
bool test_startup_drained() { return root_hooks.inspect().state == hooks::State::empty && !startup_gate.waiting(); }
void test_startup_root(uintptr_t root) { root_detour_entry(root); }
#endif
void cancel_native_installation() { startup_gate.cancel(); }
SubmissionResult submit_native_backup(const std::shared_ptr<BackupJob>& job, std::string_view directory) {
    return submit_native_save(session().native_writes, job, directory, submission_calls);
}
} // namespace sentinel::save
