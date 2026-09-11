// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_native_hooks.h"
#include "save_collector.h"
#include "save_delete.h"
#include "save_write.h"
#include "save_profile.h"
#include "save_provider.h"
#include "save_catalog.h"
#include "save_profile_prerequisite.h"
#include "save_readback.h"
#include "native_target.h"
#include "MinHook.h"
#include <intrin.h>
#include <cstring>

namespace sentinel::save {
namespace {
SubmissionCalls submission_calls{};
NativeSaveFactory original_save_factory = nullptr;
SaveReference* save_factory_detour(uintptr_t manager, SaveReference* out, uint32_t user, uintptr_t request) {
    return native_save_factory(session().native_writes, reinterpret_cast<uintptr_t>(_ReturnAddress()),
        submission_calls.factory_caller, manager, out, user, request, original_save_factory);
}
using RootInit = void (*)(uintptr_t);
RootInit original_root = nullptr;
CollectorCalls calls{};
DeleteCalls delete_calls{};
DeleteCalls scoped_delete_calls{};
DeleteOperation original_auxiliary_operation = nullptr;
DeleteOperationResult* auxiliary_operation_detour(uintptr_t context, DeleteOperationResult* out) {
    engine::LocalMemory memory;
    return delete_auxiliary_scoped(session(), memory, context, out, original_auxiliary_operation);
}
WriteCalls write_calls{};
WriteCalls read_calls{};
WriteCalls erase_calls{}, auxiliary_calls{};
CreateEnumeration original_enumeration = nullptr;
ProfileCalls profile_calls{};
ProviderCalls provider_calls{};
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
void account_removed_detour(uintptr_t callback, uintptr_t manager, uintptr_t user) {
    // A removed native account may later reuse an identity address. Refuse the
    // transition before the manager releases its cached references, even if the
    // process-wide Steam RemoteStorage singleton has not changed address.
    session().provider_reset(manager);
    original_account_removed(callback, manager, user);
}
void provider_reset_detour(uintptr_t manager) {
    session().provider_reset(manager); // Fault before native invalidation; retain routed jobs.
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
    engine::LocalMemory memory;
    return prepare_write_job(session(), memory, source, out, identity, original_prepare_job, sdk_calls.image_base);
}
SaveFuture** write_context_detour(SaveFuture** out, SaveReference* identity, const char* name, uint8_t clear, uint64_t* files) {
    engine::LocalMemory memory;
    return create_write_context(session(), memory, out, identity, name, clear, files, original_write_context, sdk_calls.image_base);
}
void destroy_preparation_detour(uintptr_t object) { destroy_write_job(session(), object, original_destroy_preparation); }
void destroy_preflight_detour(uintptr_t object) { destroy_write_job(session(), object, original_destroy_preflight); }
SdkWriteResult* sdk_poll_detour(uintptr_t context, SdkWriteResult* out, void* executor) {
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
void prepare_profile_detour(SaveReference* profile, SaveReference* data, uintptr_t shell, const char* suffix) {
    engine::LocalMemory memory;
    prepare_profile_write(session(), memory, profile, data, shell, suffix, original_prepare_profile,
        catalog_calls.retain, profile_calls);
}
WritePreflightResult* preflight_detour(uintptr_t context, WritePreflightResult* out) {
    engine::LocalMemory memory;
    return preflight_scoped(session(), memory, context, out, original_preflight, sdk_calls.image_base);
}
std::atomic<bool> owner_pinned{false};
SaveFuture** existence_detour(SaveFuture** out, SaveReference* identity, const char* name) {
    engine::LocalMemory memory;
    return query_exists_scoped(session(), memory, out, identity, name, existence_calls);
}
void root_provider_ready() {
    engine::LocalMemory memory;
    bind_root_provider(session(), memory, provider_calls);
}
void root_detour(uintptr_t root) {
    const bool entered = session().startup_enter(root,
        reinterpret_cast<uintptr_t>(_ReturnAddress()), GetCurrentThreadId());
    __try { original_root(root); if (entered) root_provider_ready(); }
    __finally { if (entered) session().startup_leave(AbnormalTermination() != FALSE); }
}
void provider_detour(uintptr_t manager) {
    original_provider(manager);
    engine::LocalMemory memory;
    provider_initialized(session(), memory, manager, provider_calls);
}
CollectorResult* collector_detour(const CollectorContext* context, CollectorResult* out) {
    engine::LocalMemory memory;
    return collect_scoped(session(), memory, context, out, calls);
}
DeleteResult* delete_detour(DeleteFuture* future, DeleteResult* out, void* executor) {
    engine::LocalMemory memory;
    return refuse_unscoped_delete(session(), memory, future, out, executor, delete_calls);
}
DeleteResult* scoped_delete_detour(DeleteFuture* future, DeleteResult* out, void* executor) {
    engine::LocalMemory memory;
    return poll_scoped_delete(session(), memory, future, out, executor, scoped_delete_calls);
}
SaveFuture** write_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return write_scoped(session(), memory, provider, out, identity, data, write_calls);
}
SaveFuture** read_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return read_scoped(session(), memory, provider, out, identity, data, read_calls);
}
SaveFuture** catalog_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data, const char* prefix) {
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return enumerate_provider(session(), memory, provider, out, identity, data, prefix, catalog_calls);
}
SaveFuture** prerequisite_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    engine::LocalMemory memory;
    return profile_read_prerequisite(session(), memory, provider, out, identity, data, prerequisite_calls);
}
SaveFuture** erase_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return delete_scoped(session(), memory, provider, out, identity, data, erase_calls, false);
}
SaveFuture** auxiliary_detour(uintptr_t provider, SaveFuture** out, uintptr_t identity, SaveReference* data) {
    if (session().routed()) session().provider_operation(provider, identity);
    engine::LocalMemory memory;
    return delete_scoped(session(), memory, provider, out, identity, data, auxiliary_calls, true);
}
EnumerationFuture** enumeration_detour(EnumerationFuture** out, SaveReference* identity, const char* root, const char* prefix) {
    engine::LocalMemory memory;
    return enumerate_scoped(session(), memory, out, identity, root, prefix, original_enumeration);
}
uint64_t profile_read_detour(SaveReference* profile, SaveReference* data) {
    engine::LocalMemory memory;
    return read_profile(session(), memory, profile, data, profile_calls);
}
uint32_t profile_serialize_detour(uintptr_t manager, uintptr_t profile, ProfileHolder* holder) {
    engine::LocalMemory memory;
    return serialize_profile(session(), memory, manager, profile, holder, profile_calls);
}
bool profile_payload(Session& owner, engine::Memory& memory, uintptr_t data) {
    return profile_payload_valid(owner, memory, data, profile_calls);
}
bool reference_copy_entry(engine::Memory& memory, const engine::Image& image) {
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
    DWORD64 module = 0;
    const auto caller = RtlLookupFunctionEntry(image.base + 0x148de8c, &module, nullptr);
    if (!caller || module != image.base || caller->BeginAddress != 0x148dd10 || caller->EndAddress < 0x148de91) return false;
    constexpr std::array<uint8_t, 5> expected_call{0xe8, 0x7f, 0x96, 0xed, 0xfe};
    std::array<uint8_t, 5> call{}; std::array<uint8_t, 88> body{};
    if (memory.copy(image.base + 0x148de8c, call.data(), call.size()).reason || call != expected_call ||
        memory.copy(image.base + 0x367510, body.data(), body.size()).reason) return false;
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < body.size(); ++i)
        if (body[i] != static_cast<uint8_t>(digit(hex[2 * i]) * 16 + digit(hex[2 * i + 1]))) return false;
    return true;
}
}
void configure_prelaunch() {
    if (session().configured()) return;
    constexpr wchar_t variable[] = L"SENTINEL_AP_TEST_SESSION";
    SetLastError(ERROR_SUCCESS);
    const DWORD needed = GetEnvironmentVariableW(variable, nullptr, 0);
    if (!needed && GetLastError() == ERROR_ENVVAR_NOT_FOUND) return;
    if (!needed || needed > 32761) { session().reject(SessionFault::descriptor); return; }
    std::wstring path(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(variable, path.data(), needed);
    if (!written || written >= needed) { session().reject(SessionFault::descriptor); return; }
    path.resize(written);
    storage::Descriptor descriptor;
    auto result = storage::read_descriptor_file(path.c_str(), descriptor);
    std::unique_ptr<storage::Namespace> lease;
    if (result.ok()) result = storage::reopen(descriptor, lease);
    if (result.ok()) {
        // The one-use descriptor/lease owner must survive even when startup is
        // missed or binding fails before any native hook becomes reachable.
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&configure_prelaunch), &module)) {
            session().reject(SessionFault::installation); return;
        }
        owner_pinned.store(true, std::memory_order_release);
    }
    if (result.ok()) result = session().configure(descriptor, std::move(lease));
    if (!result.ok()) session().reject(SessionFault::descriptor);
}
bool owner_retained() { return owner_pinned.load(std::memory_order_acquire); }
void install_native_hooks(const engine::Binding& binding, HANDLE stop) {
    if (session().state() != SessionState::prepared) return;
    if (binding.metadata.profile != SC_PROFILE_STEAM_20260818 ||
        binding.root != binding.image.base + 0x45ea6f0) {
        session().reject(SessionFault::installation); return;
    }
    engine::LocalMemory memory;
    std::array<native::Target, 45> targets;
    for (unsigned i = 0; i < targets.size(); ++i) {
        targets[i] = native::save_target(binding.image.base, i);
        if (native::validate_target(memory, binding.image, targets[i], stop, GetTickCount64() + 3000)) {
            session().reject(SessionFault::installation); return;
        }
    }
    if (!reference_copy_entry(memory, binding.image)) { session().reject(SessionFault::installation); return; }
    constexpr std::array<uint8_t, 5> expected_factory_call{0xe8, 0x3c, 0x13, 0xe2, 0x00};
    std::array<uint8_t, 5> factory_call{};
    if (!binding.image.contains(0x67473f, 5, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_WRITE) ||
        memory.copy(binding.image.base + 0x67473f, factory_call.data(), factory_call.size()).reason ||
        factory_call != expected_factory_call) { session().reject(SessionFault::installation); return; }
    InitializeSteamContext context_init = nullptr;
    if (memory.copy(binding.image.base + 0x2a1cc60, &context_init, sizeof(context_init)).reason || !context_init) {
        session().reject(SessionFault::installation); return;
    }
    constexpr unsigned hooks[] = {0, 1, 4, 6, 8, 9, 13, 14, 15, 16, 17, 18, 23, 24, 25, 26, 27, 28, 29, 30, 32, 33, 34, 35, 37, 38, 40, 41, 42, 43, 44};
    void* originals[std::size(hooks)]{};
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
    unsigned created = 0;
    for (; created < std::size(hooks); ++created) {
        if (MH_CreateHook(reinterpret_cast<void*>(targets[hooks[created]].address),
                detours[created], &originals[created]) != MH_OK) break;
    }
    if (created != std::size(hooks)) {
        for (unsigned i = 0; i < created; ++i) MH_RemoveHook(reinterpret_cast<void*>(targets[hooks[i]].address));
        session().reject(SessionFault::installation); return;
    }
    original_root = reinterpret_cast<RootInit>(originals[0]);
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
    session().install(binding.root, binding.image.base + 0x4323fc, steam_20260818_routes);
    // All immutable pointers and policy are ready BEFORE the one-time startup
    // hook is reachable. The existing observer accepting flag is independent.
    for (unsigned index : {1u, 4u, 6u, 8u, 9u, 13u, 14u, 15u, 16u, 17u, 18u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 32u, 33u, 34u, 35u, 37u, 38u, 40u, 41u, 42u, 43u, 44u, 0u}) {
        std::array<uint8_t, 32> current{};
        if (memory.copy(targets[index].address, current.data(), current.size()).reason ||
            std::memcmp(current.data(), targets[index].bytes.data(), current.size()) ||
            MH_EnableHook(reinterpret_cast<void*>(targets[index].address)) != MH_OK) {
            // Reachable trampolines stay pinned. No partial install can admit.
            session().reject(SessionFault::installation); return;
        }
    }
}
SubmissionResult submit_native_backup(const std::shared_ptr<BackupJob>& job, std::string_view directory) {
    return submit_native_save(session().native_writes, job, directory, submission_calls);
}
} // namespace sentinel::save
