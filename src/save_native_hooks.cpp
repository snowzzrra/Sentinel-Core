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
#include "MinHook.h"
#include <intrin.h>
#include <cstring>

namespace sentinel::save {
namespace {
SubmissionCalls submission_calls{};
NativeSaveFactory original_save_factory = nullptr;
SaveReference* save_factory_detour(uintptr_t manager, SaveReference* out, uint32_t user, uintptr_t request) {
    return campaign_save_factory(reinterpret_cast<uintptr_t>(_ReturnAddress()),
        submission_calls.factory_caller, manager, out, user, request, original_save_factory);
}
using RootInit = void (*)(uintptr_t);
RootInit original_root = nullptr;
StartupGate startup_gate;
uintptr_t startup_image=0, startup_expected_root=0, startup_expected_caller=0;
CollectorCalls calls{};
DeleteCalls delete_calls{};
DeleteCalls scoped_delete_calls{};
DeleteOperation original_auxiliary_operation = nullptr;
ProviderCalls provider_calls{};
DeleteOperationResult* auxiliary_operation_detour(uintptr_t context, DeleteOperationResult* out) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
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
void account_removed_detour(uintptr_t callback, uintptr_t manager, uintptr_t user) {
    // A removed native account may later reuse an identity address. Refuse the
    // transition before the manager releases its cached references, even if the
    // process-wide Steam RemoteStorage singleton has not changed address.
    observe_provider_invalidation(manager,reinterpret_cast<uintptr_t>(_ReturnAddress()),2);
    original_account_removed(callback, manager, user);
}
void provider_reset_detour(uintptr_t manager) {
    observe_provider_invalidation(manager,reinterpret_cast<uintptr_t>(_ReturnAddress()),1);
    original_provider_reset(manager);
}
WritePreflight original_preflight = nullptr;
SdkWriteCalls sdk_calls{};
SdkWriteCallback original_sdk_callback = nullptr;
DestroySdkVector original_sdk_vector = nullptr;
PrepareWriteJob original_prepare_job = nullptr;
CreateWriteContext original_write_context = nullptr;
DestroySdkVector original_destroy_preparation = nullptr, original_destroy_preflight = nullptr;
uintptr_t* prepare_job_detour(const SaveReference* source, uintptr_t* out, const SaveReference* identity) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return prepare_write_job(session(), memory, source, out, identity, original_prepare_job, sdk_calls.image_base);
}
SaveFuture** write_context_detour(SaveFuture** out, SaveReference* identity, const char* name, uint8_t clear, uint64_t* files) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return create_write_context(session(), memory, out, identity, name, clear, files, original_write_context, sdk_calls.image_base);
}
void destroy_preparation_detour(uintptr_t object) { destroy_write_job(session(), object, original_destroy_preparation); }
void destroy_preflight_detour(uintptr_t object) { destroy_write_job(session(), object, original_destroy_preflight); }
SdkWriteResult* sdk_poll_detour(uintptr_t context, SdkWriteResult* out, void* executor) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
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
void prepare_profile_detour(SaveReference* profile, SaveReference* data, uintptr_t native_user, const char* suffix) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    prepare_profile_write(session(), memory, profile, data, native_user, suffix, original_prepare_profile,
        catalog_calls.retain, profile_calls);
}
WritePreflightResult* preflight_detour(uintptr_t context, WritePreflightResult* out) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return preflight_scoped(session(), memory, context, out, original_preflight, sdk_calls.image_base);
}
std::atomic<bool> owner_pinned{false};
SaveFuture** existence_detour(SaveFuture** out, SaveReference* identity, const char* name) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return query_exists_scoped(session(), memory, out, identity, name, existence_calls);
}
void root_provider_ready() {
    engine::LocalMemory memory;
    bind_root_provider(session(), memory, provider_calls);
}
void invoke_root(uintptr_t root,bool entered) {
    __try { original_root(root); if (entered) root_provider_ready(); }
    __finally { if (entered) session().startup_leave(AbnormalTermination() != FALSE); }
}
void root_detour(uintptr_t root) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
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
    // RootInit has a void ABI, so installation refusal cannot fabricate a native
    // success by skipping initialization. Existing lower routing guards remain.
    invoke_root(root,entered);
}
void provider_detour(uintptr_t manager) {
    original_provider(manager);
    engine::LocalMemory memory;
    provider_initialized(session(), memory, manager, provider_calls);
}
CollectorResult* collector_detour(const CollectorContext* context, CollectorResult* out) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return collect_scoped(session(), memory, context, out, calls);
}
DeleteResult* delete_detour(DeleteFuture* future, DeleteResult* out, void* executor) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return refuse_unscoped_delete(session(), memory, future, out, executor, delete_calls);
}
DeleteResult* scoped_delete_detour(DeleteFuture* future, DeleteResult* out, void* executor) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return poll_scoped_delete(session(), memory, future, out, executor, scoped_delete_calls);
}
SaveFuture** write_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    NativeRouteScope route(caller, provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return campaign_write_provider(caller, memory, provider, out, identity, data, write_calls);
}
SaveFuture** read_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return read_scoped(session(), memory, provider, out, identity, data, read_calls);
}
SaveFuture** catalog_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data, const char* prefix) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return enumerate_provider(session(), memory, provider, out, identity, data, prefix, catalog_calls);
}
SaveFuture** prerequisite_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    engine::LocalMemory memory;
    return profile_read_prerequisite(session(), memory, provider, out, identity, data, prerequisite_calls);
}
SaveFuture** erase_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return delete_scoped(session(), memory, provider, out, identity, data, erase_calls, false);
}
SaveFuture** auxiliary_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return delete_scoped(session(), memory, provider, out, identity, data, auxiliary_calls, true);
}
EnumerationFuture** enumeration_detour(EnumerationFuture** out, SaveReference* identity, const char* root, const char* prefix) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return enumerate_scoped(session(), memory, out, identity, root, prefix, original_enumeration);
}
uint64_t profile_read_detour(SaveReference* profile, SaveReference* data) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return read_profile(session(), memory, profile, data, profile_calls);
}
uint32_t profile_serialize_detour(uintptr_t manager, uintptr_t profile, ProfileHolder* holder) {
    NativeRouteScope route(reinterpret_cast<uintptr_t>(_ReturnAddress()), provider_calls.image_base);
    engine::LocalMemory memory;
    return serialize_profile(session(), memory, manager, profile, holder, profile_calls);
}
bool profile_payload(Session& owner, engine::Memory& memory, uintptr_t data) {
    return profile_payload_valid(owner, memory, data, profile_calls);
}
bool checked_bytes(engine::Memory& memory, uintptr_t address, const uint8_t* expected, size_t count, sc_install_event& event) {
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
        // The one-use descriptor/lease owner must survive even when startup is
        // missed or binding fails before any native hook becomes reachable.
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
    constexpr std::array<uint8_t, 22> checkpoint_call{
        0x49,0x8b,0x02,0x4c,0x8d,0x4c,0x24,0x38,0x4d,0x8b,0xc7,
        0x48,0x8d,0x54,0x24,0x58,0x49,0x8b,0xca,0xff,0x50,0x18};
    event = record.begin(SC_INSTALL_FACTORY_CALL, 3, SC_INSTALL_UNKNOWN, 0x14897ad);
    const bool checkpoint_ok = image.contains(event.rva, checkpoint_call.size(),
        IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE) &&
        checked_bytes(memory, image.base + event.rva, checkpoint_call.data(), checkpoint_call.size(), event);
    record.finish(event, checkpoint_ok ? SC_NATIVE_NONE : SC_NATIVE_TARGET_BYTES);
    if (!checkpoint_ok) return false;
    // Only this checked native callback may delegate the shared PROFILE presence query before routing.
    constexpr std::array<uint8_t,5> presence_call{0xe8,0x26,0x2f,0xff,0xff};
    event=record.begin(SC_INSTALL_FACTORY_CALL,3,SC_INSTALL_UNKNOWN,0x1be4b35);
    const bool presence_ok=image.contains(event.rva,5,IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_EXECUTE,IMAGE_SCN_MEM_WRITE) &&
        checked_bytes(memory,image.base+event.rva,presence_call.data(),presence_call.size(),event);
    record.finish(event,presence_ok?SC_NATIVE_NONE:SC_NATIVE_TARGET_BYTES);
    if (!presence_ok) return false;
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
    std::array<native::Target, 45> targets;
    // RootInit must become reachable before the remaining save/campaign scans.
    // native::start already pins this module before entering this installer.
    if (!startup_gate.arm()) {
        session().btrace.record(BStage::startup_gate,BStatus::refused,"startup_gate_event_creation_failed",0,
            {{"win32",GetLastError()}});
        session().reject(SessionFault::installation); return;
    }
    StartupGate::Completion gate_completion(startup_gate,&session().btrace);
    const auto cancelled=[&] {
        if (!stop || WaitForSingleObject(stop,0)==WAIT_TIMEOUT) return false;
        gate_completion.cancel(); return true;
    };
    startup_image=binding.image.base; startup_expected_root=binding.root;
    startup_expected_caller=binding.image.base+0x4323fc;
    targets[0]=native::save_target(binding.image.base,0);
    if (native::validate_recorded(record,memory,binding.image,targets[0],stop,GetTickCount64()+3000,2,0)) {
        cancelled(); session().reject(SessionFault::installation); return;
    }
    void* root_original=nullptr;
    if (record.hook(SC_INSTALL_SAVE_CREATE,2,0,static_cast<uint32_t>(targets[0].address-binding.image.base),[&] {
            return MH_CreateHook(reinterpret_cast<void*>(targets[0].address),reinterpret_cast<void*>(root_detour),&root_original); })!=MH_OK) {
        session().reject(SessionFault::installation); return;
    }
    original_root=reinterpret_cast<RootInit>(root_original);
    StartupGate::Arm arm{}; arm.at_ms=GetTickCount64();
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
            arm.init_read_reason=init_read.reason; arm.init_equals_target=!init_read.reason && dispatch_init==targets[0].address;
        }
    }
    startup_gate.observe_arm(arm);
    // Presence is evidence only: neither a null manager nor a matching dispatch
    // can retrospectively qualify a native startup that already passed.
    startup_gate.record(session().btrace,BStatus::pending,arm.manager_present?
        "startup_gate_manager_already_published":"startup_gate_arming",StartupGate::State::pending);
    event=record.begin(SC_INSTALL_SAVE_ENABLE,2,0,static_cast<uint32_t>(targets[0].address-binding.image.base));
    const bool root_cancelled=cancelled();
    if (root_cancelled || !checked_bytes(memory,targets[0].address,targets[0].bytes.data(),32,event)) {
        record.finish(event,root_cancelled?SC_NATIVE_CANCELLED:SC_NATIVE_TARGET_BYTES);
        record.hook(SC_INSTALL_REMOVE,2,0,static_cast<uint32_t>(targets[0].address-binding.image.base),[&] {
            return MH_RemoveHook(reinterpret_cast<void*>(targets[0].address)); });
        session().reject(SessionFault::installation); return;
    }
    const auto root_enabled=MH_EnableHook(reinterpret_cast<void*>(targets[0].address));
    record.finish(event,root_enabled==MH_OK?SC_NATIVE_NONE:SC_NATIVE_HOOK_FAILED,static_cast<uint32_t>(root_enabled));
    if (root_enabled!=MH_OK) {
        record.hook(SC_INSTALL_REMOVE,2,0,static_cast<uint32_t>(targets[0].address-binding.image.base),[&] {
            return MH_RemoveHook(reinterpret_cast<void*>(targets[0].address)); });
        session().reject(SessionFault::installation); return;
    }
    // From this point RootInit/trampoline/event remain process-lifetime objects.
    // No early-return cleanup may remove this reachable gate.
    for (unsigned i = 1; i < targets.size(); ++i) {
        targets[i] = native::save_target(binding.image.base, i);
        if (native::validate_recorded(record, memory, binding.image, targets[i], stop, GetTickCount64() + 3000, 2, i)) {
            cancelled(); session().reject(SessionFault::installation); return;
        }
    }
    uintptr_t initializer = 0;
    if (!validate_native_helpers(record, memory, binding.image, initializer)) { session().reject(SessionFault::installation); return; }
    const auto context_init = reinterpret_cast<InitializeSteamContext>(initializer);
    constexpr unsigned hooks[] = {0, 1, 4, 6, 8, 9, 13, 14, 15, 16, 17, 18, 23, 24, 25, 26, 27, 28, 29, 30, 32, 33, 34, 35, 37, 38, 40, 41, 42, 43, 44};
    void* originals[std::size(hooks)]{};
    originals[0]=root_original;
    void* detours[] = {reinterpret_cast<void*>(root_detour), reinterpret_cast<void*>(collector_detour),
        reinterpret_cast<void*>(delete_detour), reinterpret_cast<void*>(write_detour),
        reinterpret_cast<void*>(profile_read_detour), reinterpret_cast<void*>(profile_serialize_detour),
        reinterpret_cast<void*>(read_detour), reinterpret_cast<void*>(enumeration_detour),
        reinterpret_cast<void*>(erase_detour), reinterpret_cast<void*>(auxiliary_detour),
        reinterpret_cast<void*>(provider_detour), reinterpret_cast<void*>(catalog_detour),
        reinterpret_cast<void*>(preflight_detour), reinterpret_cast<void*>(prepare_profile_detour),
        reinterpret_cast<void*>(clear_data_detour), reinterpret_cast<void*>(destroy_data_detour),
        reinterpret_cast<void*>(existence_detour), reinterpret_cast<void*>(sdk_poll_detour),
        reinterpret_cast<void*>(sdk_callback_detour), reinterpret_cast<void*>(sdk_vector_detour),
        reinterpret_cast<void*>(prepare_job_detour), reinterpret_cast<void*>(destroy_preparation_detour),
        reinterpret_cast<void*>(write_context_detour), reinterpret_cast<void*>(destroy_preflight_detour),
        reinterpret_cast<void*>(read_preparation_detour), reinterpret_cast<void*>(read_decode_detour),
        reinterpret_cast<void*>(save_factory_detour), reinterpret_cast<void*>(scoped_delete_detour),
        reinterpret_cast<void*>(auxiliary_operation_detour), reinterpret_cast<void*>(provider_reset_detour),
        reinterpret_cast<void*>(account_removed_detour)};
    static_assert(std::size(detours) == std::size(hooks));
    unsigned created = 1;
    for (; created < std::size(hooks); ++created) {
        const auto index = hooks[created];
        if (record.hook(SC_INSTALL_SAVE_CREATE, 2, index, static_cast<uint32_t>(targets[index].address - binding.image.base), [&] {
                return MH_CreateHook(reinterpret_cast<void*>(targets[index].address), detours[created], &originals[created]); }) != MH_OK) break;
    }
    if (created != std::size(hooks)) {
        for (unsigned i = 1; i < created; ++i) record.hook(SC_INSTALL_REMOVE, 2, hooks[i],
            static_cast<uint32_t>(targets[hooks[i]].address - binding.image.base), [&] {
                return MH_RemoveHook(reinterpret_cast<void*>(targets[hooks[i]].address)); });
        session().reject(SessionFault::installation); return;
    }
    original_provider = reinterpret_cast<InitializeProvider>(originals[10]);
    original_preflight = reinterpret_cast<WritePreflight>(originals[12]);
    original_prepare_profile = reinterpret_cast<PrepareProfile>(originals[13]);
    original_clear_data = reinterpret_cast<ClearSaveData>(originals[14]);
    original_destroy_data = reinterpret_cast<ClearSaveData>(originals[15]);
    provider_calls = {binding.image.base, context_init};
    sdk_calls = {reinterpret_cast<PollSdkWrite>(originals[17]), binding.image.base, context_init};
    original_sdk_callback = reinterpret_cast<SdkWriteCallback>(originals[18]);
    original_sdk_vector = reinterpret_cast<DestroySdkVector>(originals[19]);
    original_prepare_job = reinterpret_cast<PrepareWriteJob>(originals[20]);
    original_destroy_preparation = reinterpret_cast<DestroySdkVector>(originals[21]);
    original_write_context = reinterpret_cast<CreateWriteContext>(originals[22]);
    original_destroy_preflight = reinterpret_cast<DestroySdkVector>(originals[23]);
    original_read_preparation = reinterpret_cast<PrepareRead>(originals[24]);
    original_read_decode = reinterpret_cast<DecodeRead>(originals[25]);
    original_save_factory = reinterpret_cast<NativeSaveFactory>(originals[26]);
    existence_calls = {provider_calls, reinterpret_cast<QueryExists>(originals[16]),
        reinterpret_cast<ReleaseSaveReference>(targets[5].address)};
    calls = {reinterpret_cast<Collect>(originals[1]),
        reinterpret_cast<AssignString>(targets[2].address), reinterpret_cast<ReleaseVector>(targets[3].address)};
    delete_calls = {reinterpret_cast<PollDelete>(originals[2]), reinterpret_cast<ReleaseDelete>(targets[5].address)};
    scoped_delete_calls = {reinterpret_cast<PollDelete>(originals[27]), delete_calls.release};
    original_auxiliary_operation = reinterpret_cast<DeleteOperation>(originals[28]);
    original_provider_reset = reinterpret_cast<InitializeProvider>(originals[29]);
    original_account_removed = reinterpret_cast<AccountRemoved>(originals[30]);
    write_calls = {reinterpret_cast<CreateWrite>(originals[3]), reinterpret_cast<SetSaveName>(targets[7].address),
        reinterpret_cast<ReleaseSaveReference>(targets[5].address), profile_payload, binding.image.base};
    submission_calls = {reinterpret_cast<NativeSave>(targets[39].address), write_calls.release,
        binding.root, binding.image.base + 0x674744};
    read_calls = {reinterpret_cast<CreateWrite>(originals[6]), write_calls.set_name, write_calls.release, nullptr, binding.image.base};
    catalog_calls = {reinterpret_cast<EnumerateProvider>(originals[11]),
        reinterpret_cast<RetainSaveReference>(binding.image.base + 0x367510), write_calls.release,
        write_calls.set_name, calls.assign, binding.image.base};
    prerequisite_calls = {reinterpret_cast<AllocateNative>(targets[20].address),
        reinterpret_cast<ConstructSaveData>(targets[21].address), reinterpret_cast<DestroySaveData>(targets[22].address),
        catalog_calls, read_calls.create};
    readback_calls = {prerequisite_calls,
        reinterpret_cast<decltype(ReadbackCalls::construct_stream)>(targets[36].address)};
    write_calls.readback = &readback_calls;
    read_calls.profile_read = prerequisite_detour;
    original_enumeration = reinterpret_cast<CreateEnumeration>(originals[7]);
    erase_calls = {reinterpret_cast<CreateWrite>(originals[8]), write_calls.set_name, write_calls.release, nullptr, binding.image.base};
    auxiliary_calls = {reinterpret_cast<CreateWrite>(originals[9]), write_calls.set_name, write_calls.release, nullptr, binding.image.base};
    profile_calls = {reinterpret_cast<ReadProfile>(originals[4]), reinterpret_cast<SerializeProfile>(originals[5]),
        reinterpret_cast<LookupProfileValue>(targets[10].address), reinterpret_cast<DestroyProfileValue>(targets[11].address),
        reinterpret_cast<ProfileChecksum>(targets[12].address), reinterpret_cast<ReleaseSaveReference>(targets[5].address), binding.image.base};
    // The installed production modules determine the gate, never configuration or
    // IPC. Unsupported provider/reset transitions retain AP ownership and fail.
    event = record.begin(SC_INSTALL_POLICY);
    session().install(binding.root, binding.image.base + 0x4323fc, steam_20260818_routes);
    const bool published = session().inspect().prepared_routes == steam_20260818_routes;
    record.finish(event, published ? SC_NATIVE_NONE : SC_NATIVE_CANCELLED);
    if (!published) { session().reject(SessionFault::installation); return; }
    if (!install_campaign_hooks(binding, stop)) { cancelled(); session().reject(SessionFault::installation); return; }
    // Publish every immutable pointer/policy/hook before releasing RootInit.
    // Target0 is already patched: do not revalidate or enable it a second time.
    for (unsigned index : {1u, 4u, 6u, 8u, 9u, 13u, 14u, 15u, 16u, 17u, 18u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 32u, 33u, 34u, 35u, 37u, 38u, 40u, 41u, 42u, 43u, 44u}) {
        if (cancelled()) { session().reject(SessionFault::installation); return; }
        event = record.begin(SC_INSTALL_SAVE_ENABLE, 2, index, static_cast<uint32_t>(targets[index].address - binding.image.base));
        if (!checked_bytes(memory, targets[index].address, targets[index].bytes.data(), 32, event)) {
            record.finish(event, SC_NATIVE_TARGET_BYTES);
            session().reject(SessionFault::installation); return;
        }
        const auto enabled = MH_EnableHook(reinterpret_cast<void*>(targets[index].address));
        record.finish(event, enabled == MH_OK ? SC_NATIVE_NONE : SC_NATIVE_HOOK_FAILED, static_cast<uint32_t>(enabled));
        if (enabled != MH_OK) {
            // Reachable trampolines stay pinned. No partial install can admit.
            session().reject(SessionFault::installation); return;
        }
    }
    if (cancelled()) { session().reject(SessionFault::installation); return; }
    record.finish(record.begin(SC_INSTALL_READY));
    gate_completion.ready();
}
SubmissionResult submit_native_backup(const std::shared_ptr<BackupJob>& job, std::string_view directory) {
    return submit_native_save(session().native_writes, job, directory, submission_calls);
}
} // namespace sentinel::save
