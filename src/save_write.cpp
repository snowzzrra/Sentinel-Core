// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_write.h"
#include "save_collector.h"
#include "save_readback.h"
#include "save_submission.h"
#include "save_b_io_trace.h"
#include <array>
#include <cstring>
#include <memory>
#include <new>

namespace sentinel::save {
namespace {
static_assert(sizeof(SaveResult) == 24 && offsetof(SaveResult, value) == 16);
static_assert(sizeof(SaveReference) == 8 && sizeof(SaveFutureVtable) == 16);
SaveFuture* destroy_refusal(SaveFuture* future, uint32_t) { return future; }
SaveResult* poll_refusal(SaveFuture*, SaveResult* out, void*) {
    *out = {0, 1, 1, 0}; // Ready / saveGameError, never success or fallback 0x40.
    return out;
}
const SaveFutureVtable refusal_vtable{destroy_refusal, poll_refusal};
// Immutable and stateless: each consumer can poll/destroy independently. The
// pinned Core owns the object and both methods for the remaining process life;
// the deleting-destructor call never hands this storage to the native allocator.
SaveFuture refusal{&refusal_vtable};

enum class Access { read, write, erase, erase_auxiliary };
struct AccessFuture : SaveFuture {
    Session& owner; SessionFault failure; Access access = Access::read; SaveFuture* native = nullptr; bool terminal = false;
    ProfileWrite selection{};
    uint64_t operation = 0;
    SaveFuture* readback = nullptr; bool native_done = false; SaveResult native_result{};
    AccessFuture(Session& session, SessionFault fault) : SaveFuture{}, owner(session), failure(fault) {}
    ~AccessFuture() {
        if (native) native->vtable->destroy(native, 1);
        if (readback) readback->vtable->destroy(readback, 1);
        if (!terminal && native) owner.btrace.record(BStage::provider, BStatus::blocked, "provider_destroyed_before_terminal", operation,
            {{"native_done", native_done}, {"readback_owned", readback != nullptr}, {"access", access}});
        owner.native_writes.close_provider(operation);
    }
};
SaveFuture* destroy_access(SaveFuture* future, uint32_t) { delete static_cast<AccessFuture*>(future); return future; }
SaveResult* poll_access(SaveFuture* base, SaveResult* out, void* task) {
    auto& future = *static_cast<AccessFuture*>(base);
    if (future.terminal) { *out = {1, 0, 0, 0}; return out; }
    SaveResult result = future.native_result;
    // Poll the existing native operation, including after inspection shutdown or
    // a later route fault. Dropping it here would invent completion of its jobs.
    if (!future.native_done) {
        WritePollScope scope(future.owner.native_writes, future.operation);
        future.native->vtable->poll(future.native, &result, task);
        future.owner.native_writes.provider_result(future.operation, result);
        future.native_done = result.state != -1;
        future.native_result = result;
        const bool success = !result.state && !result.outcome && result.value == 1;
        future.owner.btrace.record(BStage::provider, result.state == -1 ? BStatus::pending : success ? BStatus::succeeded : BStatus::refused,
            "provider_native_result", future.operation, {{"state", result.state}, {"outcome", result.state == 0 ? result.outcome : 0},
            {"value", result.state == 0 ? result.value : 0}, {"access", future.access}});
    }
    if (result.state == -1) { *out = result; return out; }
    if (!result.state && !result.outcome && result.value == 1 && future.readback) {
        future.readback->vtable->poll(future.readback, &result, task);
        if (result.state == -1) {
            engine::LocalMemory memory; future.owner.campaign_run.write_observed(future.operation, false, false, memory);
            *out = result; return out;
        }
    }
    future.terminal = true;
    { engine::LocalMemory memory;
      future.owner.campaign_run.write_observed(future.operation, true, !result.state && !result.outcome && result.value == 1, memory); }
    if (result.state != 0 || result.outcome != 0 || result.value != 1) {
        future.owner.fail(future.failure); result = {0, 1, 1, 0};
    } else if (future.selection.sequence) {
        engine::LocalMemory memory;
        if (!future.owner.persist_profile_write(future.selection, memory)) {
            future.owner.btrace.record(BStage::profile_publish, BStatus::refused, "profile_selection_persist_failed", future.operation,
                {{"selection_sequence", future.selection.sequence}});
            future.owner.fail_profile(); result = {0, 1, 1, 0};
        } else future.owner.btrace.record(BStage::profile_publish, BStatus::succeeded, "profile_selection_persisted", future.operation,
            {{"selection_sequence", future.selection.sequence}});
    }
    *out = result; return out;
}
const SaveFutureVtable access_vtable{destroy_access, poll_access};
bool read_name(engine::Memory& memory, uintptr_t data, std::string& name, int32_t limit = 63, bool empty = false,
        BIoTrace trace = {nullptr, BStage::provider}) {
    NativeString text{}; std::array<char, 256> buffer{};
    if (!trace.read(memory, data, 0, text, "directory_header_unreadable") ||
        !trace.check(text.data && text.length >= (empty ? 0 : 1) && text.length <= limit, "directory_extent_invalid",
            {{"length", text.length}, {"limit", limit}, {"null_bytes", !text.data}, {"empty_allowed", empty}})) return false;
    const auto count = static_cast<size_t>(text.length);
    if (!trace.copy(memory, reinterpret_cast<uintptr_t>(text.data), buffer.data(), count + 1, "directory_bytes_unreadable") ||
        !trace.check(!buffer[count] && !std::memchr(buffer.data(), 0, count), "directory_termination_invalid", {{"length", count}})) return false;
    name.assign(buffer.data(), count); return true;
}
bool relative_files(engine::Memory& memory, uintptr_t data, const WriteCalls& calls, bool root_auxiliary, BIoTrace trace) {
    uintptr_t files = 0; int32_t count = 0, capacity = 0;
    if (!trace.read(memory, data, 0x1c0, files, "provider_files_unreadable") ||
        !trace.read(memory, data, 0x1c8, count, "provider_count_unreadable") ||
        !trace.read(memory, data, 0x1cc, capacity, "provider_capacity_unreadable") ||
        !trace.check(count >= 0 && count <= 1024 && capacity >= count && (!count || files), "provider_vector_invalid",
            {{"count", count}, {"capacity", capacity}, {"null_files", !files}})) return false;
    for (int32_t i = 0; i < count; ++i) {
        uintptr_t file = 0, vtable = 0; std::string name;
        if (!trace.read(memory, files, static_cast<uintptr_t>(i) * 8, file, "provider_stream_unreadable") ||
            !trace.read(memory, file, 0, vtable, "provider_stream_vtable_unreadable") ||
            !trace.check(vtable == calls.image_base + 0x2a575a8, "provider_stream_type_mismatch", {{"file_index", i}, {"vtable_rva", vtable - calls.image_base}}) ||
            !read_name(memory, file + 8, name, 191, false, trace)) return false;
        if (!trace.check(!root_auxiliary || !steam_name_equal(std::string_view(name).substr(0, 9), "sentinel-"),
            "auxiliary_targets_root_record", {{"file_index", i}})) return false;
        size_t begin = 0;
        for (size_t n = 0; n <= name.size(); ++n) {
            if (n == name.size() || name[n] == '/') {
                const auto component = std::string_view(name).substr(begin, n - begin);
                if (!trace.check(!component.empty() && component != "." && component != "..", "relative_file_component_invalid",
                    {{"file_index", i}, {"component_offset", begin}, {"component_length", component.size()}})) return false;
                begin = n + 1;
            } else {
                const unsigned char c = static_cast<unsigned char>(name[n]);
                if (!trace.check((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.', "relative_file_character_invalid",
                    {{"file_index", i}, {"character_offset", n}, {"character", c}})) return false;
            }
        }
    }
    if (root_auxiliary) {
        std::string prefix, suffix;
        if (!read_name(memory, data + 0x70, prefix, 7, true, trace) ||
            !read_name(memory, data + 0xa8, suffix, 7, true, trace)) return false;
        if (!trace.check(prefix.empty() || suffix.empty() ||
            (!steam_name_equal(std::string_view(prefix).substr(0, 9), "sentinel-") &&
             !steam_name_equal(std::string_view("sentinel-").substr(0, prefix.size()), prefix)), "auxiliary_mask_targets_root_records",
             {{"prefix_length", prefix.size()}, {"suffix_length", suffix.size()}})) return false;
    }
    return true;
}
bool campaign_slot(std::string_view name) {
    constexpr std::string_view prefixes[]{"GAME-AUTOSAVE", "DLC1-AUTOSAVE", "DLC2-AUTOSAVE"};
    for (auto prefix : prefixes) {
        if (!steam_name_equal(name.substr(0, prefix.size()), prefix)) continue;
        const auto number = name.substr(prefix.size());
        return (number.size() == 1 && number[0] >= '0' && number[0] <= '9') ||
            number == "10" || number == "11";
    }
    return false;
}
bool prepare_name(Session& owner, engine::Memory& memory, SaveReference* reference,
                  const WriteCalls& calls, Access access, bool& profile, ProfileWrite& selection,
                  uintptr_t& data, std::string& name) {
    BIoTrace trace{&owner.btrace, BStage::provider, 0, reference->control};
    if (!trace.read(memory, reference->control, 8, data, "provider_source_unreadable") ||
        !read_name(memory, data, name, 63, access == Access::erase_auxiliary, trace)) return false;
    if (name == "PROFILE") {
        profile = true;
        if (access == Access::read) return true;
        if (!trace.check(access == Access::write, "profile_mutation_kind_forbidden", {{"access", access}}) ||
            !trace.check(calls.profile(owner, memory, data), "profile_payload_validation_failed") ||
            !trace.check(owner.take_profile_write(data, selection), "profile_write_capture_missing")) return false;
        return true;
    }
    if (!trace.check(owner.native_io(), "provider_native_io_not_admitted", {{"session_state", owner.state()}, {"session_fault", owner.fault()}})) return false;
    const bool root_auxiliary = access == Access::erase_auxiliary && (name.empty() || name == owner.native_root());
    if (access != Access::erase && !relative_files(memory, data, calls, root_auxiliary, trace)) return false;
    if (root_auxiliary) {
        if (name.empty()) {
            calls.set_name(data, owner.native_root().c_str());
            if (!read_name(memory, data, name, 63, false, trace) ||
                !trace.check(name == owner.native_root(), "auxiliary_root_set_name_mismatch")) return false;
        }
        return true;
    }
    const std::string prefix = owner.native_root() + "/";
    const bool scoped = name.compare(0, prefix.size(), prefix) == 0;
    const auto slot = scoped ? std::string_view(name).substr(prefix.size()) : std::string_view(name);
    if (!trace.check(campaign_slot(slot), "provider_campaign_slot_invalid", {{"scoped", scoped}, {"name_length", name.size()}, {"slot_length", slot.size()}})) return false;
    if (!scoped) {
        const auto target = prefix + name;
        if (!trace.check(target.size() < 64, "provider_scoped_name_too_long", {{"length", target.size()}, {"limit", 63}})) return false;
        calls.set_name(data, target.c_str());
        if (!read_name(memory, data, name, 63, false, trace) ||
            !trace.check(name == target, "provider_set_name_mismatch", {{"actual_length", name.size()}, {"expected_length", target.size()}})) return false;
    }
    return true;
}
UnroutedSource unrouted_source(engine::Memory& memory, SaveReference* reference) {
    UnroutedSource result;
    const auto copy = [&](uintptr_t base, size_t offset, void* out, size_t size) {
        ++result.step;
        uintptr_t address = 0;
        if (!engine::add(base, offset, size, address)) { result.reason = SC_REASON_OUT_OF_RANGE; return false; }
        const auto read = memory.copy(address, out, size);
        result.reason = read.reason; result.error = read.error; return !read.reason;
    };
    SaveReference ref{}; NativeString name{};
    result.kind = 1; // Invalid/unreadable; zero means no source metadata was requested.
    if (!copy(reinterpret_cast<uintptr_t>(reference), 0, &ref, sizeof(ref)) ||
        !copy(ref.control, 8, &result.data, sizeof(result.data)) ||
        !copy(result.data, 0, &name, sizeof(name))) return result;
    result.length = name.length;
    if (name.length < 0 || name.length >= static_cast<int32_t>(result.name.size())) {
        result.reason = SC_REASON_OUT_OF_RANGE; return result;
    }
    if (!copy(reinterpret_cast<uintptr_t>(name.data), 0, result.name.data(), static_cast<size_t>(name.length)+1)) return result;
    if (result.name[name.length] || std::memchr(result.name.data(), 0, static_cast<size_t>(name.length))) {
        result.reason = SC_REASON_OUT_OF_RANGE; return result;
    }
    result.step = 5;
    const std::string_view text(result.name.data(), static_cast<size_t>(name.length));
    result.kind = text == "PROFILE" ? 2u : campaign_slot(text) ? 3u : text.substr(0,3) == "ap-" ? 4u : 5u;
    return result;
}
SaveFuture** access_scoped(Session& owner, engine::Memory& memory, uintptr_t provider,
        SaveFuture** out, uintptr_t identity, SaveReference* reference, const WriteCalls& calls, Access access,
        bool native_checkpoint = false) {
    owner.native_writes.bind_diagnostics(owner.btrace);
    owner.btrace.record(BStage::provider, BStatus::entered, "provider_factory_entered", 0,
        {{"access", access}, {"native_checkpoint", native_checkpoint}, {"session_state", owner.state()}, {"session_fault", owner.fault()}}, provider);
    if (!owner.routed()) {
        UnroutedSource source;
        if (owner.state() == SessionState::prepared || owner.state() == SessionState::starting)
            source = unrouted_source(memory, reference);
        owner.unrouted_import(access == Access::read ? "provider_read" : "provider_mutation",
            access == Access::read ? "import" : "mutate", provider, identity, source); // Metadata never permits late admission.
        if (owner.state() == SessionState::disabled) return calls.create(provider, out, identity, reference);
        owner.btrace.record(BStage::provider, BStatus::blocked, "provider_route_unavailable", 0,
            {{"access", access}, {"session_state", owner.state()}, {"session_fault", owner.fault()}});
        calls.release(reference); *out = refused_save_future(); return out;
    }
    bool prepared = false, profile = false; ProfileWrite selection{};
    uintptr_t data = 0; std::string name;
    const auto failure = access == Access::read ? SessionFault::native_read : access == Access::write ?
        SessionFault::native_write : SessionFault::unscoped_delete;
    try { prepared = prepare_name(owner, memory, reference, calls, access, profile, selection, data, name); }
    catch (const std::bad_alloc&) { owner.btrace.record(BStage::provider, BStatus::refused, "provider_name_allocation_failed"); }
    if (prepared) {
        prepared = owner.campaign_run.allow_access(data, name, access == Access::write,
            access == Access::erase || access == Access::erase_auxiliary);
        if (!prepared) owner.btrace.record(BStage::provider, BStatus::blocked, "campaign_access_refused", 0,
            {{"access", access}, {"profile", profile}, {"session_fault", owner.fault()}}, data);
    }
    if (prepared) {
        if (profile && access == Access::read && calls.profile_read) return calls.profile_read(provider, out, identity, reference);
        auto future = std::unique_ptr<AccessFuture>(new (std::nothrow) AccessFuture(owner, failure));
        if (!future) owner.btrace.record(BStage::provider, BStatus::refused, "provider_wrapper_allocation_failed");
        if (future) future->access = access;
        if (future && access == Access::write) {
            future->operation = owner.native_writes.open_provider(data, name);
            if (!future->operation) future.reset();
            else if (!profile && !capture_submission(owner.native_writes, future->operation, name)) {
                owner.btrace.record(BStage::provider, BStatus::refused, "provider_submission_not_correlated", future->operation); future.reset();
            }
            if (future && !owner.campaign_run.write_started(future->operation, name,
                native_checkpoint || capture_native_checkpoint(owner.native_writes))) {
                owner.btrace.record(BStage::provider, BStatus::blocked, "campaign_write_start_refused", future->operation); future.reset();
            }
        }
        if (future) {
            if (access == Access::write && calls.readback) {
                future->readback = create_write_readback(owner, future->operation, provider, identity, name, *calls.readback);
                if (!future->readback) {
                    owner.fail(failure); calls.release(reference); *out = refused_save_future(); return out;
                }
            }
            future->selection = selection;
            calls.create(provider, &future->native, identity, reference); // Consumes its reference, even on failure.
            if (future->native) {
                owner.btrace.record(BStage::provider, BStatus::pending, "provider_native_future_created", future->operation,
                    {{"access", access}, {"profile", profile}, {"selection_sequence", selection.sequence}}, data);
                future->vtable = &access_vtable; *out = future.release(); return out;
            }
            owner.btrace.record(BStage::provider, BStatus::refused, "provider_native_factory_null", future->operation, {{"access", access}});
            owner.fail(failure); *out = refused_save_future(); return out;
        }
    }
    owner.fail(failure);
    // Match the native factory's consumed strong reference. Other references
    // and the lock remain in the native caller, which creates its normal task.
    calls.release(reference);
    *out = refused_save_future();
    return out;
}
}
SaveFuture* refused_save_future() { return &refusal; }
WritePreflightResult* preflight_scoped(Session& owner, engine::Memory& memory, uintptr_t context,
        WritePreflightResult* out, WritePreflight original, uintptr_t image) {
    static_assert(sizeof(WritePreflightResult) == 32);
    if (!owner.routed()) { owner.unrouted_import("write_preflight", "mutate"); if (owner.state() == SessionState::disabled) return original(context, out); }
    owner.native_writes.bind_diagnostics(owner.btrace);
    BIoTrace trace{&owner.btrace, BStage::sdk_prepare, 0, context};
    owner.btrace.record(BStage::sdk_prepare, BStatus::entered, "preflight_entered", 0, {}, context);
    bool scoped = false;
    try {
        uintptr_t remote = 0; std::string name; std::array<uint64_t, 3> files{};
        if (trace.check(context && context <= UINTPTR_MAX - 0x58, "preflight_context_invalid") &&
            trace.read(memory, context, 0, remote, "preflight_remote_unreadable") &&
            trace.read(memory, context, 0x40, files, "preflight_vector_unreadable") &&
            trace.check(files[0] && files[1] && files[1] <= files[2] && files[1] <= UINTPTR_MAX / 0x180,
                "preflight_vector_invalid", {{"count", files[1]}, {"capacity", files[2]}, {"null_files", !files[0]}}) &&
            trace.check(owner.collecting(remote, owner.native_root()), "preflight_remote_not_owned") &&
            read_name(memory, context + 8, name, 63, false, trace)) {
            const std::string prefix = owner.native_root() + "/";
            scoped = trace.check(name == "PROFILE" || (name.compare(0, prefix.size(), prefix) == 0 &&
                campaign_slot(std::string_view(name).substr(prefix.size()))), "preflight_directory_not_scoped", {{"name_length", name.size()}});
            if (scoped) {
                const auto sequence = owner.native_writes.begin(remote, files[0], files[1], name);
                scoped = sequence && prepare_sdk_payloads(owner.native_writes, memory, sequence, files[0], files[1], name, image, &owner.btrace);
            }
        }
    } catch (const std::bad_alloc&) { owner.btrace.record(BStage::sdk_prepare, BStatus::refused, "preflight_allocation_failed", 0, {}, context); }
    if (scoped) {
        auto* result = original(context, out);
        owner.btrace.record(BStage::sdk_prepare, result->tag == 0 ? BStatus::succeeded : BStatus::refused,
            "preflight_native_returned", 0, {{"native_tag", result->tag},
            {"error_kind", result->tag == 1 ? result->first : 0}, {"error_code", result->tag == 1 ? result->second : 0}}, context);
        return result;
    }
    owner.fail(SessionFault::native_write);
    // Same native error alternative as the null-provider branch, using error1.
    // No vector is moved: the native context still owns and destroys its inputs.
    *out = {1, 0, 1, 0}; return out;
}
SaveFuture** write_scoped(Session& owner, engine::Memory& memory, uintptr_t provider,
        SaveFuture** out, uintptr_t identity, SaveReference* reference, const WriteCalls& calls, bool native_checkpoint) {
    return access_scoped(owner, memory, provider, out, identity, reference, calls, Access::write, native_checkpoint);
}
SaveFuture** read_scoped(Session& owner, engine::Memory& memory, uintptr_t provider,
        SaveFuture** out, uintptr_t identity, SaveReference* reference, const WriteCalls& calls) {
    return access_scoped(owner, memory, provider, out, identity, reference, calls, Access::read);
}
SaveFuture** delete_scoped(Session& owner, engine::Memory& memory, uintptr_t provider,
        SaveFuture** out, uintptr_t identity, SaveReference* reference, const WriteCalls& calls, bool auxiliary) {
    return access_scoped(owner, memory, provider, out, identity, reference, calls,
        auxiliary ? Access::erase_auxiliary : Access::erase);
}
} // namespace sentinel::save
