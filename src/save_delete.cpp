// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_delete.h"
#include "save_collector.h"
#include <cstring>

namespace sentinel::save {
namespace {
struct DeleteContext {
    uintptr_t storage, save_data, task;
    int64_t result;
    uint64_t payload, job;
};
static_assert(sizeof(DeleteContext) == 0x30 && offsetof(DeleteContext, job) == 0x28);
static_assert(sizeof(DeleteResult) == 24);
// Inspected prefix through the job handle; the native allocation is 0x70.
struct DirectoryDeleteContext {
    uintptr_t storage;
    NativeString directory;
    uintptr_t task;
    int64_t result;
    uint64_t payload, job;
};
static_assert(sizeof(DirectoryDeleteContext) == 0x58 && offsetof(DirectoryDeleteContext, task) == 0x38 &&
    offsetof(DirectoryDeleteContext, result) == 0x40 && offsetof(DirectoryDeleteContext, job) == 0x50);
bool campaign_directory(Session& owner, engine::Memory& memory, const NativeString& name) {
    std::array<char, 64> text{};
    if (!name.data || name.length <= 0 || name.length >= 64 ||
        memory.copy(reinterpret_cast<uintptr_t>(name.data), text.data(), static_cast<size_t>(name.length) + 1).reason ||
        text[static_cast<size_t>(name.length)] || std::memchr(text.data(), 0, static_cast<size_t>(name.length))) return false;
    std::string_view directory(text.data(), static_cast<size_t>(name.length));
    const auto& root = owner.native_root();
    if (directory.size() <= root.size() || directory.substr(0, root.size()) != root || directory[root.size()] != '/') return false;
    directory.remove_prefix(root.size() + 1);
    if (directory.size() < 14 || native_campaign_index(directory.substr(0, 5)) < 0 ||
        !steam_name_equal(directory.substr(5, 8), "AUTOSAVE")) return false;
    const auto slot = directory.substr(13);
    return (slot.size() == 1 && slot[0] >= '0' && slot[0] <= '9') || slot == "10" || slot == "11";
}
}
DeleteResult* refuse_unscoped_delete(Session& owner, engine::Memory& memory,
        DeleteFuture* future, DeleteResult* output, void* executor, const DeleteCalls& calls) {
    if (!owner.routed()) { owner.unrouted_import(); return calls.poll(future, output, executor); }
    DeleteFuture reference{}; uintptr_t object = 0; DeleteContext context{};
    const bool readable = memory.copy(reinterpret_cast<uintptr_t>(future), &reference, sizeof(reference)).reason == 0 &&
        reference.control && reference.control <= UINTPTR_MAX - 8 &&
        memory.copy(reference.control + 8, &object, sizeof(object)).reason == 0 && object &&
        memory.copy(object, &context, sizeof(context)).reason == 0;
    if (readable && context.job == 0 && context.result == -1) {
        // This exact future allocates/enqueues its worker only in the original
        // first poll. Consume its owned strong reference as terminal poll does,
        // then publish an outer-ready/inner-error result, never boolean success.
        owner.fail(SessionFault::unscoped_delete);
        calls.release(future);
        *output = {0, 1, 1, 0};
        return output;
    }
    owner.fail(SessionFault::delete_indeterminate);
    if (readable && context.job != 0) {
        // A job already exists. The original only observes/drains that handle;
        // cancellation cannot establish that deletion did not execute. Preserve
        // its result and the process lease; never restart a zero-handle future.
        return calls.poll(future, output, executor);
    }
    // Unknown native ownership is not ours to destroy. Outer cancellation is
    // not a completion receipt; the session records indeterminate and stays pinned.
    *output = {1, 0, 0, 0};
    return output;
}
DeleteResult* poll_scoped_delete(Session& owner, engine::Memory& memory,
        DeleteFuture* future, DeleteResult* output, void* executor, const DeleteCalls& calls) {
    if (!owner.routed()) { owner.unrouted_import(); return calls.poll(future, output, executor); }
    DeleteFuture reference{}; uintptr_t object = 0; DirectoryDeleteContext context{};
    const bool readable = memory.copy(reinterpret_cast<uintptr_t>(future), &reference, sizeof(reference)).reason == 0 &&
        reference.control && reference.control <= UINTPTR_MAX - 8 &&
        memory.copy(reference.control + 8, &object, sizeof(object)).reason == 0 && object &&
        memory.copy(object, &context, sizeof(context)).reason == 0;
    const bool allowed = readable && owner.native_io() && owner.collecting(context.storage, owner.native_root()) &&
        campaign_directory(owner, memory, context.directory);
    if (readable && context.job != 0) {
        // Only the zero-handle branch can enqueue. Even a foreign or faulted
        // job must retain native observation/cleanup; it may already have run.
        if (!allowed) owner.fail(SessionFault::delete_indeterminate);
        return calls.poll(future, output, executor);
    }
    if (readable && context.result == -1) {
        if (allowed) return calls.poll(future, output, executor);
        owner.fail(SessionFault::unscoped_delete);
        calls.release(future); *output = {0, 1, 1, 0}; return output;
    }
    owner.fail(SessionFault::delete_indeterminate);
    // A missing object or completed-looking zero-handle context does not prove
    // fresh ownership. Do not release it or restart the native worker.
    *output = {1, 0, 0, 0}; return output;
}
} // namespace sentinel::save
