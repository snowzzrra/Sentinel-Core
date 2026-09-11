// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_delete.h"

namespace sentinel::save {
namespace {
struct DeleteContext {
    uintptr_t storage, save_data, task;
    int64_t result;
    uint64_t payload, job;
};
static_assert(sizeof(DeleteContext) == 0x30 && offsetof(DeleteContext, job) == 0x28);
static_assert(sizeof(DeleteResult) == 24);
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
} // namespace sentinel::save
