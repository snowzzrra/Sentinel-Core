// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_readback.h"
#include "save_backup.h"
#include "save_b_io_trace.h"
#include <windows.h>
#include <cstring>
#include <new>

namespace sentinel::save {
namespace {
template<class T> bool at(engine::Memory& memory, uintptr_t p, size_t offset, T& out) {
    return p && p <= UINTPTR_MAX - offset && !memory.copy(p + offset, &out, sizeof(out)).reason;
}
struct NativeControl { uint32_t strong, weak; uintptr_t object; DestroySaveData destroy; };
static_assert(sizeof(NativeControl) == 24 && sizeof(ReadWorkerResult) == 16);
struct ReadbackFuture : SaveFuture {
    Session& owner; uint64_t operation; ReadbackCalls calls;
    SaveReference data{}; SaveFuture* native = nullptr;
    bool started = false, terminal = false;
    ReadbackFuture(Session& s, uint64_t id, const ReadbackCalls& c) : owner(s), operation(id), calls(c) {}
    ~ReadbackFuture() {
        if (!terminal) {
            owner.btrace.record(BStage::readback_verify, BStatus::blocked, "readback_destroyed_before_terminal", operation,
                {{"started", started}, {"native_future_owned", native != nullptr}});
            owner.native_writes.readback_result(operation, false);
        }
        if (native) native->vtable->destroy(native, 1);
        // Worker references may outlive this wrapper. The actual SaveData Clear/
        // destructor detour removes the pointer mapping, never this wrapper.
        calls.native.catalog.release(&data);
    }
};
bool populate(ReadbackFuture& future, engine::Memory& memory) {
    BIoTrace trace{&future.owner.btrace, BStage::readback_prepare, future.operation};
    SdkWriteObservation manifest;
    if (!future.owner.native_writes.readback_manifest(future.operation, manifest)) return false;
    auto* control = reinterpret_cast<NativeControl*>(future.data.control);
    const auto data = control->object; trace.source = data;
    uintptr_t files = 0; int32_t count = 0, capacity = 0;
    if (!trace.read(memory, data, 0x1c8, count, "readback_populate_count_unreadable") ||
        !trace.check(count == 0, "readback_populate_not_empty", {{"count", count}}) ||
        !trace.read(memory, data, 0x1cc, capacity, "readback_populate_capacity_unreadable")) return false;
    const auto needed = static_cast<int32_t>(manifest.payloads.size());
    if (!trace.check(capacity >= needed, "readback_populate_capacity_short", {{"capacity", capacity}, {"needed", needed}}) ||
        !trace.read(memory, data, 0x1c0, files, "readback_populate_files_unreadable") ||
        !trace.check(files != 0, "readback_populate_files_null")) return false;
    const std::string prefix = manifest.directory + "/";
    for (const auto& file : manifest.payloads) {
        const std::string name(file.name.data());
        if (!trace.check(!name.compare(0, prefix.size(), prefix) && name.size() != prefix.size(),
            "readback_manifest_file_not_scoped", {{"file_index", count}, {"name_length", name.size()}})) return false;
        void* allocation = future.calls.native.allocate(0x190);
        if (!trace.check(allocation != nullptr, "readback_stream_allocation_failed", {{"file_index", count}, {"bytes", 0x190}})) return false;
        // Preserve native ownership and the stored-byte verification flag.
        future.calls.construct_stream(allocation, name.c_str() + prefix.size(), 0x140);
        const auto stream = reinterpret_cast<uintptr_t>(allocation);
        std::memcpy(reinterpret_cast<void*>(files + static_cast<uintptr_t>(count) * 8), &stream, 8);
        ++count;
        std::memcpy(reinterpret_cast<void*>(data + 0x1c8), &count, 4);
    }
    future.owner.btrace.record(BStage::readback_prepare, BStatus::succeeded, "readback_streams_populated", future.operation,
        {{"count", count}, {"capacity", capacity}});
    return true;
}
SaveFuture* destroy(SaveFuture* value, uint32_t) { delete static_cast<ReadbackFuture*>(value); return value; }
SaveResult* poll(SaveFuture* value, SaveResult* out, void* task) {
    auto& future = *static_cast<ReadbackFuture*>(value);
    if (future.terminal) { *out = {1, 0, 0, 0}; return out; }
    bool ready = true;
    engine::LocalMemory memory;
    if (!future.started) {
        try { ready = populate(future, memory); } catch (const std::bad_alloc&) {
            future.owner.btrace.record(BStage::readback_prepare, BStatus::refused, "readback_populate_allocation_failed", future.operation); ready = false;
        }
        future.started = true;
    }
    SaveResult result{0, 1, 1, 0};
    if (ready) {
        future.owner.btrace.record(BStage::readback_verify, BStatus::entered, "readback_native_poll_entered", future.operation);
        future.native->vtable->poll(future.native, &result, task);
        future.owner.btrace.record(BStage::readback_verify, result.state == -1 ? BStatus::pending :
            !result.state && !result.outcome && result.value == 1 ? BStatus::succeeded : BStatus::refused,
            "readback_native_poll_result", future.operation, {{"state", result.state}, {"outcome", result.state == 0 ? result.outcome : 0},
            {"value", result.state == 0 ? result.value : 0}});
        if (result.state == -1) { *out = result; return out; }
    }
    future.terminal = true;
    future.owner.native_writes.readback_result(future.operation,
        !result.state && !result.outcome && result.value == 1);
    NativeWriteOperation operation;
    const bool retained = future.owner.native_writes.inspect_operation(future.operation, operation);
    if (!retained || operation.readback_failed) {
        future.owner.btrace.record(BStage::readback_verify, BStatus::refused,
            !retained ? "readback_terminal_operation_missing" : !operation.readback_hashes ? "readback_terminal_hash_proof_missing" : "readback_terminal_failed",
            future.operation, {{"retained", retained}, {"hashes_verified", operation.readback_hashes},
            {"readback_terminal", operation.readback_terminal}, {"state", result.state},
            {"outcome", result.state == 0 ? result.outcome : 0}, {"value", result.state == 0 ? result.value : 0}});
        future.owner.fail(SessionFault::native_read); result = {0, 1, 1, 0};
    }
    *out = result; return out;
}
const SaveFutureVtable vtable{destroy, poll};
using FileSize = int32_t (*)(uintptr_t, const char*);
struct SizeProxy { const void* table; uintptr_t remote; FileSize size; const SdkWriteObservation& manifest; Session& owner; uint64_t operation; };
int32_t file_size(uintptr_t self, const char* name) {
    const auto& proxy = *reinterpret_cast<const SizeProxy*>(self);
    for (size_t index = 0; index < proxy.manifest.payloads.size(); ++index) {
        const auto& file = proxy.manifest.payloads[index];
        if (std::strcmp(name, file.name.data())) continue;
        const auto actual = proxy.size(proxy.remote, name);
        const bool valid = actual > 0 && static_cast<uint32_t>(actual) == file.size;
        proxy.owner.btrace.record(BStage::readback_prepare, valid ? BStatus::succeeded : BStatus::refused,
            "readback_storage_file_size", proxy.operation, {{"file_index", index}, {"expected", file.size}, {"actual", actual}});
        return valid ? actual : 0;
    }
    proxy.owner.btrace.record(BStage::readback_prepare, BStatus::refused, "readback_file_not_in_manifest", proxy.operation,
        {{"manifest_count", proxy.manifest.payloads.size()}});
    return 0;
}
const std::array<void*, 16> size_table{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void*>(file_size)};
bool ordinary_directory(Session& owner, engine::Memory& memory, uintptr_t data, BIoTrace trace) {
    NativeString name{}; std::array<char, 64> text{};
    if (!trace.read(memory, data, 0, name, "read_directory_header_unreadable") ||
        !trace.check(name.data && name.length > 0 && name.length < 64, "read_directory_extent_invalid",
            {{"length", name.length}, {"null_bytes", !name.data}}) ||
        !trace.copy(memory, reinterpret_cast<uintptr_t>(name.data), text.data(), static_cast<size_t>(name.length) + 1, "read_directory_bytes_unreadable") ||
        !trace.check(!text[static_cast<size_t>(name.length)] && !std::memchr(text.data(), 0, static_cast<size_t>(name.length)),
            "read_directory_termination_invalid", {{"length", name.length}})) return false;
    std::string_view directory(text.data(), static_cast<size_t>(name.length));
    if (directory == "PROFILE") return true;
    const auto& root = owner.native_root();
    if (!trace.check(directory.size() > root.size() && directory.substr(0, root.size()) == root && directory[root.size()] == '/',
        "read_directory_namespace_mismatch", {{"length", directory.size()}, {"root_length", root.size()}})) return false;
    directory.remove_prefix(root.size() + 1);
    if (!trace.check(directory.size() >= 14 && native_campaign_index(directory.substr(0, 5)) >= 0 &&
        steam_name_equal(directory.substr(5, 8), "AUTOSAVE"), "read_directory_campaign_invalid", {{"length", directory.size()}})) return false;
    const auto slot = directory.substr(13);
    return trace.check((slot.size() == 1 && slot[0] >= '0' && slot[0] <= '9') || slot == "10" || slot == "11",
        "read_directory_slot_invalid", {{"slot_length", slot.size()}});
}
ReadWorkerResult* invoke_prepare(PrepareRead original, uintptr_t context, ReadWorkerResult* out,
        SaveReference* waiter, uintptr_t proxy, uintptr_t remote, BTrace* diagnostics, uint64_t operation) {
    auto* field = reinterpret_cast<uintptr_t*>(context); remote = *field; *field = proxy;
    bool returned = false;
    __try { auto* result = original(context, out, waiter); returned = true; return result; }
    __finally {
        *field = remote;
        if (!returned) diagnostics->record(BStage::readback_prepare, BStatus::refused, "readback_native_prepare_exception", operation, {}, context);
    }
}
}
SaveFuture* create_write_readback(Session& owner, uint64_t id, uintptr_t provider, uintptr_t identity,
        const std::string& directory, const ReadbackCalls& calls) {
    owner.native_writes.bind_diagnostics(owner.btrace);
    BIoTrace trace{&owner.btrace, BStage::readback_create, id};
    owner.btrace.record(BStage::readback_create, BStatus::entered, "readback_create_entered", id, {{"directory_length", directory.size()}});
    auto future = std::unique_ptr<ReadbackFuture>(new (std::nothrow) ReadbackFuture(owner, id, calls));
    if (!trace.check(future != nullptr, "readback_wrapper_allocation_failed")) return nullptr;
    auto* control = static_cast<NativeControl*>(calls.native.allocate(sizeof(NativeControl)));
    if (!trace.check(control != nullptr, "readback_control_allocation_failed", {{"bytes", sizeof(NativeControl)}})) return nullptr;
    *control = {1, 1, 0, calls.native.destroy}; future->data.control = reinterpret_cast<uintptr_t>(control);
    void* object = calls.native.allocate(0x280);
    if (!trace.check(object != nullptr, "readback_data_allocation_failed", {{"bytes", 0x280}})) return nullptr;
    owner.btrace.record(BStage::readback_create, BStatus::entered, "readback_native_construct", id);
    calls.native.construct(object); control->object = reinterpret_cast<uintptr_t>(object);
    owner.btrace.record(BStage::readback_create, BStatus::entered, "readback_native_set_name", id, {}, control->object);
    calls.native.catalog.set_name(control->object, directory.c_str());
    const int32_t loading = 1; const uint8_t enabled = 1;
    std::memcpy(static_cast<char*>(object) + 0x278, &loading, 4);
    std::memcpy(static_cast<char*>(object) + 0x27d, &enabled, 1);
    int32_t capacity = 0;
    std::memcpy(&capacity, static_cast<char*>(object) + 0x1cc, 4);
    if (!owner.native_writes.attach_readback(id, control->object, capacity)) return nullptr;
    owner.btrace.record(BStage::readback_create, BStatus::entered, "readback_native_retain", id, {}, control->object);
    SaveReference argument{}; calls.native.catalog.retain(&argument, &future->data);
    owner.btrace.record(BStage::readback_create, BStatus::entered, "readback_native_factory", id, {}, control->object);
    calls.native.read(provider, &future->native, identity, &argument);
    if (!trace.check(future->native != nullptr, "readback_native_factory_null")) return nullptr;
    owner.btrace.record(BStage::readback_create, BStatus::succeeded, "readback_native_future_created", id, {{"capacity", capacity}}, control->object);
    future->vtable = &vtable; return future.release();
}
ReadWorkerResult* prepare_readback(Session& owner, engine::Memory& memory, uintptr_t context,
        ReadWorkerResult* out, SaveReference* waiter, PrepareRead original) {
    BIoTrace trace{owner.routed() ? &owner.btrace : nullptr, BStage::readback_prepare, 0, context};
    uintptr_t control = 0, data = 0;
    const bool source = trace.read(memory, context, 8, control, "read_prepare_control_unreadable") &&
        trace.read(memory, control, 8, data, "read_prepare_data_unreadable");
    const auto id = source ? owner.native_writes.readback_operation(data) : 0;
    if (!id) {
        if (!owner.routed()) return original(context, out, waiter);
        uintptr_t remote = 0;
        if (source && trace.check(owner.native_io(), "read_prepare_native_io_not_admitted", {{"session_state", owner.state()}, {"session_fault", owner.fault()}}) &&
            trace.read(memory, context, 0, remote, "read_prepare_remote_unreadable") &&
            trace.check(owner.collecting(remote, owner.native_root()), "read_prepare_remote_not_owned") &&
            ordinary_directory(owner, memory, data, trace)) {
            const bool profile = owner.is_profile_request(data);
            if (profile) owner.profile_step(ProfileStage::prepare, ProfileStatus::entered, "native_profile_prepare");
            auto* result = original(context, out, waiter);
            if (profile) owner.profile_step(ProfileStage::prepare,
                result->outcome == 0 ? ProfileStatus::succeeded : ProfileStatus::refused,
                "native_profile_prepare_result", true, 0, result->outcome, result->active_value());
            owner.btrace.record(BStage::readback_prepare, result->outcome == 0 ? BStatus::succeeded : BStatus::refused,
                "ordinary_native_read_prepare_result", 0, {{"profile", profile}, {"outcome", result->outcome}, {"value", result->active_value()}}, data);
            return result;
        }
        if (owner.is_profile_request(data)) owner.profile_step(ProfileStage::prepare, ProfileStatus::refused, "profile_remote_or_directory_gate");
        owner.fail(SessionFault::native_read);
        auto* result = invoke_prepare(original, context, out, waiter, 0, remote, &owner.btrace, id);
        result->value = 1; return result;
    }
    trace.trace = &owner.btrace; trace.operation = id;
    SdkWriteObservation manifest; uintptr_t remote = 0, table = 0; FileSize size = nullptr; bool valid = false;
    try {
        valid = owner.native_writes.readback_manifest(id, manifest) &&
            trace.read(memory, context, 0, remote, "readback_remote_unreadable") &&
            trace.check(remote == manifest.remote, "readback_remote_identity_mismatch") &&
            trace.read(memory, remote, 0, table, "readback_remote_vtable_unreadable") &&
            trace.read(memory, table, 0x78, size, "readback_file_size_function_unreadable") &&
            trace.check(size != nullptr, "readback_file_size_function_null");
    } catch (const std::bad_alloc&) { trace.check(false, "readback_prepare_allocation_failed"); }
    // Preserve the native weak waiter cleanup even for a failed manifest.
    if (!valid) manifest.payloads.clear();
    SizeProxy proxy{size_table.data(), remote, size, manifest, owner, id};
    auto* result = invoke_prepare(original, context, out, waiter, reinterpret_cast<uintptr_t>(&proxy), remote, &owner.btrace, id);
    owner.btrace.record(BStage::readback_prepare, result->outcome == 0 ? BStatus::succeeded : BStatus::refused,
        "readback_native_prepare_result", id, {{"manifest_valid", valid}, {"outcome", result->outcome}, {"value", result->active_value()}});
    return result;
}
ReadWorkerResult* verify_readback(Session& owner, engine::Memory& memory, uintptr_t context,
        ReadWorkerResult* out, DecodeRead original, uintptr_t image) {
    BIoTrace trace{owner.routed() ? &owner.btrace : nullptr, BStage::readback_verify, 0, context, BStatus::blocked};
    uintptr_t control = 0, data = 0;
    if (!trace.read(memory, context, 0, control, "read_decode_control_unreadable") ||
        !trace.read(memory, control, 8, data, "read_decode_data_unreadable")) return original(context, out);
    const auto id = owner.native_writes.readback_operation(data);
    if (!id) {
        if (!owner.campaign_run.verify_source(memory, data, image)) { *out = {1, 1, 0}; return out; }
        const bool profile = owner.is_profile_request(data);
        if (profile) owner.profile_step(ProfileStage::decode, ProfileStatus::entered, "native_decode_entered");
        auto* result = original(context, out);
        if (profile) owner.profile_step(ProfileStage::decode,
            result->outcome == 0 ? ProfileStatus::succeeded : ProfileStatus::refused,
            result->outcome == 0 ? "native_decode_completed" : result->active_value() == 4 ? "native_authentication_refused" : "native_decode_result",
            true, 0, result->outcome, result->active_value());
        if (owner.routed()) owner.btrace.record(BStage::readback_verify, result->outcome == 0 ? BStatus::succeeded : BStatus::refused,
            "ordinary_native_decode_result", 0, {{"profile", profile}, {"outcome", result->outcome}, {"value", result->active_value()}}, data);
        return result;
    }
    trace.trace = &owner.btrace; trace.operation = id; trace.source = data; trace.failure_status = BStatus::refused;
    owner.btrace.record(BStage::readback_verify, BStatus::entered, "readback_verify_entered", id, {}, data);
    bool valid = false; SdkWriteObservation manifest; std::vector<uintptr_t> buffers;
    try {
        uintptr_t files = 0; int32_t count = 0; uint8_t cancelled = 0;
        valid = owner.native_writes.readback_manifest(id, manifest) &&
            trace.read(memory, data, 0x1c0, files, "readback_files_unreadable") &&
            trace.check(files != 0, "readback_files_null") &&
            trace.read(memory, data, 0x1c8, count, "readback_count_unreadable") &&
            trace.check(count > 0 && static_cast<size_t>(count) == manifest.payloads.size(), "readback_count_mismatch",
                {{"actual", count}, {"expected", manifest.payloads.size()}}) &&
            trace.read(memory, data, 0x27f, cancelled, "readback_cancelled_unreadable") &&
            trace.check(!cancelled, "readback_native_cancelled", {{"cancelled", cancelled}});
        const auto deadline = GetTickCount64() + 1000;
        for (int32_t i = 0; valid && i < count; ++i) {
            uintptr_t stream = 0, table = 0, buffer = 0; uint64_t size = 0, capacity = 0;
            uint32_t flags = 0, status = 0; NativeString name{}; std::array<char, 260> text{};
            std::array<unsigned char, 32> digest{}; const auto& expected = manifest.payloads[static_cast<size_t>(i)];
            valid = trace.read(memory, files, static_cast<size_t>(i) * 8, stream, "readback_stream_unreadable") &&
                trace.read(memory, stream, 0, table, "readback_stream_vtable_unreadable") &&
                trace.check(table == image + 0x2a575a8, "readback_stream_type_mismatch", {{"file_index", i}, {"vtable_rva", table - image}}) &&
                trace.read(memory, stream, 0x180, flags, "readback_stream_flags_unreadable") &&
                trace.check(flags == 0x140, "readback_stream_flags_mismatch", {{"file_index", i}, {"expected", 0x140}, {"actual", flags}}) &&
                trace.read(memory, stream, 0x184, status, "readback_stream_status_unreadable") &&
                trace.check(!status, "readback_stream_error", {{"file_index", i}, {"native_status", status}}) &&
                trace.read(memory, stream, 8, name, "readback_name_header_unreadable") &&
                trace.check(name.data && name.length > 0 && name.length < 260, "readback_name_extent_invalid",
                    {{"file_index", i}, {"length", name.length}, {"null_bytes", !name.data}}) &&
                trace.copy(memory, reinterpret_cast<uintptr_t>(name.data), text.data(), static_cast<size_t>(name.length) + 1, "readback_name_bytes_unreadable") &&
                trace.check(!text[static_cast<size_t>(name.length)] && !std::memchr(text.data(), 0, static_cast<size_t>(name.length)),
                    "readback_name_termination_invalid", {{"file_index", i}, {"length", name.length}}) &&
                trace.check(manifest.directory + "/" + text.data() == expected.name.data(), "readback_manifest_name_mismatch", {{"file_index", i}}) &&
                trace.read(memory, stream, 0x150, size, "readback_size_unreadable") &&
                trace.check(size == expected.size, "readback_loaded_size_mismatch", {{"file_index", i}, {"expected", expected.size}, {"actual", size}}) &&
                trace.read(memory, stream, 0x158, capacity, "readback_capacity_unreadable") &&
                trace.check(size <= capacity, "readback_buffer_capacity_short", {{"file_index", i}, {"size", size}, {"capacity", capacity}}) &&
                trace.read(memory, stream, 0x168, buffer, "readback_buffer_unreadable") &&
                digest_payload(memory, buffer, expected.size, digest, deadline, &owner.btrace, BStage::readback_verify, id, static_cast<uint64_t>(i));
            if (valid && digest != expected.sha256) {
                size_t changed = 0; while (changed < digest.size() && digest[changed] == expected.sha256[changed]) ++changed;
                valid = trace.check(false, "readback_sha256_mismatch", {{"file_index", i}, {"bytes", expected.size},
                    {"digest_byte_index", changed}, {"expected_digest_byte", expected.sha256[changed]}, {"actual_digest_byte", digest[changed]}});
            }
            if (valid) buffers.push_back(buffer);
        }
    } catch (const std::bad_alloc&) { valid = false; trace.check(false, "readback_verify_allocation_failed"); }
    if (!valid) { owner.native_writes.readback_result(id, false); *out = {1, 1, 0}; return out; }
    owner.native_writes.readback_hashes(id);
    owner.btrace.record(BStage::readback_verify, BStatus::succeeded, "readback_hashes_verified", id, {{"count", buffers.size()}});
    auto* result = original(context, out);
    owner.btrace.record(BStage::readback_verify, !result->outcome && result->active_value() == 1 ? BStatus::succeeded : BStatus::refused,
        "readback_native_decode_result", id, {{"outcome", result->outcome}, {"value", result->active_value()}, {"count", buffers.size()}});
    if (!result->outcome && result->active_value() == 1) {
        if (auto job = owner.native_writes.backup(id)) job->copy(owner, memory, manifest, buffers);
    }
    return result;
}
} // namespace sentinel::save
