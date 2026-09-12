// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_write.h"
#include "save_collector.h"
#include "save_readback.h"
#include "save_submission.h"
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
    Session& owner; SessionFault failure; SaveFuture* native = nullptr; bool terminal = false;
    ProfileWrite selection{};
    uint64_t operation = 0;
    SaveFuture* readback = nullptr; bool native_done = false; SaveResult native_result{};
    AccessFuture(Session& session, SessionFault fault) : SaveFuture{}, owner(session), failure(fault) {}
    ~AccessFuture() {
        if (native) native->vtable->destroy(native, 1);
        if (readback) readback->vtable->destroy(readback, 1);
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
            future.owner.fail_profile(); result = {0, 1, 1, 0};
        }
    }
    *out = result; return out;
}
const SaveFutureVtable access_vtable{destroy_access, poll_access};
bool read_name(engine::Memory& memory, uintptr_t data, std::string& name, int32_t limit = 63, bool empty = false) {
    NativeString text{};
    std::array<char, 256> buffer{};
    if (memory.copy(data, &text, sizeof(text)).reason || !text.data ||
        text.length < (empty ? 0 : 1) || text.length > limit) return false;
    const auto count = static_cast<size_t>(text.length);
    if (memory.copy(reinterpret_cast<uintptr_t>(text.data), buffer.data(), count + 1).reason ||
        buffer[count] || std::memchr(buffer.data(), 0, count)) return false;
    name.assign(buffer.data(), count);
    return true;
}
bool relative_files(engine::Memory& memory, uintptr_t data, const WriteCalls& calls, bool root_auxiliary) {
    uintptr_t files = 0; int32_t count = 0, capacity = 0;
    if (memory.copy(data + 0x1c0, &files, sizeof(files)).reason ||
        memory.copy(data + 0x1c8, &count, sizeof(count)).reason ||
        memory.copy(data + 0x1cc, &capacity, sizeof(capacity)).reason ||
        count < 0 || count > 1024 || capacity < count || (count && !files)) return false;
    for (int32_t i = 0; i < count; ++i) {
        uintptr_t file = 0, vtable = 0; std::string name;
        if (memory.copy(files + static_cast<uintptr_t>(i) * 8, &file, sizeof(file)).reason || !file ||
            memory.copy(file, &vtable, sizeof(vtable)).reason || vtable != calls.image_base + 0x2a575a8 ||
            !read_name(memory, file + 8, name, 191)) return false;
        if (root_auxiliary && steam_name_equal(std::string_view(name).substr(0, 9), "sentinel-")) return false;
        // The worker joins root + '/' + this relative name. Refuse path syntax
        // that can escape the owned root, including empty/traversal components.
        size_t begin = 0;
        for (size_t n = 0; n <= name.size(); ++n) {
            if (n == name.size() || name[n] == '/') {
                const auto component = std::string_view(name).substr(begin, n - begin);
                if (component.empty() || component == "." || component == "..") return false;
                begin = n + 1;
            } else {
                const unsigned char c = static_cast<unsigned char>(name[n]);
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
            }
        }
    }
    if (root_auxiliary) {
        std::string prefix, suffix;
        if (!read_name(memory, data + 0x70, prefix, 7, true) ||
            !read_name(memory, data + 0xa8, suffix, 7, true)) return false;
        // Native optional deletion matches the basename, and runs only when
        // BOTH masks are nonempty. It must never reach Core's root records.
        if (!prefix.empty() && !suffix.empty() &&
            (steam_name_equal(std::string_view(prefix).substr(0, 9), "sentinel-") ||
             steam_name_equal(std::string_view("sentinel-").substr(0, prefix.size()), prefix))) return false;
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
    if (!reference->control || reference->control > UINTPTR_MAX - 8 ||
        memory.copy(reference->control + 8, &data, sizeof(data)).reason || !data ||
        !read_name(memory, data, name, 63, access == Access::erase_auxiliary)) return false;

    // Shared PROFILE keeps its name. Its actual request-owned bytes must retain
    // the independently captured vanilla selection before any provider job exists.
    if (name == "PROFILE") {
        profile = true;
        return access == Access::read || (access == Access::write && calls.profile(owner, memory, data) &&
            owner.take_profile_write(data, selection));
    }
    if (!owner.native_io()) return false;
    const bool root_auxiliary = access == Access::erase_auxiliary && (name.empty() || name == owner.native_root());
    if (access != Access::erase && !relative_files(memory, data, calls, root_auxiliary)) return false;
    if (access == Access::erase_auxiliary && (name.empty() || name == owner.native_root())) {
        if (name.empty()) {
            calls.set_name(data, owner.native_root().c_str());
            if (!read_name(memory, data, name) || name != owner.native_root()) return false;
        }
        return true;
    }
    const std::string prefix = owner.native_root() + "/";
    const bool scoped = name.compare(0, prefix.size(), prefix) == 0;
    const auto slot = scoped ? std::string_view(name).substr(prefix.size()) : std::string_view(name);
    if (!campaign_slot(slot)) return false;
    if (!scoped) {
        const auto target = prefix + name;
        // Native SetName has fixed 64-byte storage and silently empties long input.
        if (target.size() >= 64) return false;
        calls.set_name(data, target.c_str());
        if (!read_name(memory, data, name) || name != target)
            return false;
    }
    return true;
}
SaveFuture** access_scoped(Session& owner, engine::Memory& memory, uintptr_t provider,
        SaveFuture** out, uintptr_t identity, SaveReference* reference, const WriteCalls& calls, Access access) {
    if (!owner.routed()) {
        owner.unrouted_import(access == Access::read ? "provider_read" : "provider_mutation",
            access == Access::read ? "import" : "mutate", provider, identity); // An already-started native operation also closes late admission.
        if (owner.state() == SessionState::disabled) return calls.create(provider, out, identity, reference);
        calls.release(reference); *out = refused_save_future(); return out;
    }
    bool prepared = false, profile = false; ProfileWrite selection{};
    uintptr_t data = 0; std::string name;
    const auto failure = access == Access::read ? SessionFault::native_read : access == Access::write ?
        SessionFault::native_write : SessionFault::unscoped_delete;
    try { prepared = prepare_name(owner, memory, reference, calls, access, profile, selection, data, name); }
    catch (const std::bad_alloc&) {} // No native operation was created yet.
    if (prepared) prepared = owner.campaign_run.allow_access(data, name, access == Access::write,
        access == Access::erase || access == Access::erase_auxiliary);
    if (prepared) {
        if (profile && access == Access::read && calls.profile_read) return calls.profile_read(provider, out, identity, reference);
        auto future = std::unique_ptr<AccessFuture>(new (std::nothrow) AccessFuture(owner, failure));
        if (future && access == Access::write) {
            future->operation = owner.native_writes.open_provider(data, name);
            if (!future->operation || (!profile && !capture_submission(owner.native_writes, future->operation, name))) future.reset();
            if (future && !owner.campaign_run.write_started(future->operation, name, capture_native_checkpoint(owner.native_writes))) future.reset();
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
            if (future->native) { future->vtable = &access_vtable; *out = future.release(); return out; }
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
    bool scoped = false;
    try {
        uintptr_t remote = 0; std::string name; std::array<uint64_t, 3> files{};
        // This is the final owned output vector, after native preparation. An
        // empty vector can otherwise delete the old slot and finish true without
        // submitting a single SDK write. Refuse before quota/deletion or moves.
        if (context && context <= UINTPTR_MAX - 0x58 &&
            !memory.copy(context, &remote, sizeof(remote)).reason &&
            !memory.copy(context + 0x40, files.data(), sizeof(files)).reason &&
            files[0] && files[1] && files[1] <= files[2] && files[1] <= UINTPTR_MAX / 0x180 &&
            owner.collecting(remote, owner.native_root()) && read_name(memory, context + 8, name)) {
            const std::string prefix = owner.native_root() + "/";
            scoped = name == "PROFILE" || (name.compare(0, prefix.size(), prefix) == 0 &&
                campaign_slot(std::string_view(name).substr(prefix.size())));
            if (scoped) {
                const auto sequence = owner.native_writes.begin(remote, files[0], files[1], name);
                scoped = sequence && prepare_sdk_payloads(owner.native_writes, memory, sequence, files[0], files[1], name, image);
            }
        }
    } catch (const std::bad_alloc&) {}
    if (scoped) return original(context, out);
    owner.fail(SessionFault::native_write);
    // Same native error alternative as the null-provider branch, using error1.
    // No vector is moved: the native context still owns and destroys its inputs.
    *out = {1, 0, 1, 0}; return out;
}
SaveFuture** write_scoped(Session& owner, engine::Memory& memory, uintptr_t provider,
        SaveFuture** out, uintptr_t identity, SaveReference* reference, const WriteCalls& calls) {
    return access_scoped(owner, memory, provider, out, identity, reference, calls, Access::write);
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
