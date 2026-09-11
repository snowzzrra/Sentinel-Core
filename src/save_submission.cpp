// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_submission.h"
#include "save_session.h"
#include <windows.h>

namespace sentinel::save {
namespace {
struct Invocation {
    NativeWrites& owner;
    const std::shared_ptr<BackupJob>& job;
    std::string_view directory;
    SubmissionResult result;
    unsigned factories = 0, captures = 0;
    bool valid = true;
};
struct Factory { Invocation* invocation; Factory* previous; };
thread_local Invocation* submitting = nullptr;
thread_local Factory* factory = nullptr;
struct Checkpoint { NativeWrites* owner; Checkpoint* previous; unsigned captures=0; };
thread_local Checkpoint* checkpoint=nullptr;

SaveReference* invoke_factory(Factory* scope, uintptr_t manager, SaveReference* out,
        uint32_t user, uintptr_t request, NativeSaveFactory original) {
    factory = scope;
    // Mode1 native job contexts call jobs synchronously and propagate that mode
    // to nested contexts. This stack pointer never enters an asynchronous job.
    __try { return original(manager, out, user, request); }
    __finally { factory = scope->previous; }
}
void invoke(Invocation* scope, const SubmissionCalls* calls) {
    SaveReference task{};
    auto* previous = submitting;
    submitting = scope;
    __try {
        __try {
            scope->result.entered = true;
            calls->save(calls->root, &task, 0, 0, 0);
            scope->result.task_returned = task.control != 0;
        } __finally {
            // The native manager retains its own reference to the task. Releasing
            // this returned reference does not claim cancellation/completion.
            if (task.control) calls->release(&task);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        scope->result.exception = GetExceptionCode(); scope->valid = false;
    }
    submitting = previous;
}
} // namespace
SubmissionResult submit_native_save(NativeWrites& owner, const std::shared_ptr<BackupJob>& job,
        std::string_view directory, const SubmissionCalls& calls) {
    Invocation scope{owner, job, directory, {}};
    if (!job || !calls.save || !calls.release || !calls.root || !calls.factory_caller || submitting) {
        if (job) job->readback_finished(false);
        return scope.result;
    }
    invoke(&scope, &calls);
    scope.result.matched = scope.valid && scope.factories == 1 && scope.captures == 1 &&
        scope.result.operation && scope.result.task_returned && !scope.result.exception;
    if (!scope.result.matched) {
        // An already-owned native writer must keep running to its real terminal
        // state. A failed invocation cannot turn that into "did not execute".
        if (scope.result.operation) job->cancel(); else job->readback_finished(false);
    }
    return scope.result;
}
SaveReference* native_save_factory(NativeWrites& owner, uintptr_t caller, uintptr_t expected_caller,
        uintptr_t manager, SaveReference* out, uint32_t user, uintptr_t request, NativeSaveFactory original) {
    // RVA 0x1495a80 constructs its job context with mode1. Provider creation is
    // synchronous; the returned future may complete later under its own refs.
    Checkpoint current_checkpoint{caller==expected_caller?&owner:nullptr,checkpoint};
    checkpoint=&current_checkpoint;
    __try {
        if (!submitting || &submitting->owner != &owner) return original(manager, out, user, request);
        auto& scope = *submitting;
        if (caller != expected_caller || ++scope.factories != 1 || factory) scope.valid = false;
        Factory current{&scope, factory};
        return invoke_factory(&current, manager, out, user, request, original);
    } __finally { checkpoint=current_checkpoint.previous; }
}
bool capture_native_checkpoint(NativeWrites& owner) {
    return checkpoint && checkpoint->owner==&owner && ++checkpoint->captures==1;
}
bool capture_submission(NativeWrites& owner, uint64_t operation, std::string_view directory) {
    if (!factory || &factory->invocation->owner != &owner) return true;
    auto& scope = *factory->invocation;
    if (!scope.valid || ++scope.captures != 1 || !steam_name_equal(directory, scope.directory) ||
        !owner.request_backup(operation, scope.job)) {
        scope.valid = false; return false;
    }
    scope.result.operation = operation; return true;
}
} // namespace sentinel::save
