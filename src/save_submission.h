// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_backup.h"
#include "save_write.h"
#include <string_view>

namespace sentinel::save {
using NativeSave = SaveReference* (*)(uintptr_t, SaveReference*, uint8_t, uint8_t, uint8_t);
using NativeSaveFactory = SaveReference* (*)(uintptr_t, SaveReference*, uint32_t, uintptr_t);
struct SubmissionCalls { NativeSave save; ReleaseSaveReference release; uintptr_t root, factory_caller; };
struct SubmissionResult {
    uint64_t operation = 0;
    uint32_t exception = 0;
    bool entered = false, task_returned = false, matched = false;
};
// Internal executor body. The existing native callback owns operation admission.
// No address, native request structure or callback is supplied through IPC.
SubmissionResult submit_native_save(NativeWrites&, const std::shared_ptr<BackupJob>&,
    std::string_view directory, const SubmissionCalls&);
SaveReference* native_save_factory(NativeWrites&, uintptr_t caller, uintptr_t expected_caller,
    uintptr_t manager, SaveReference*, uint32_t user, uintptr_t request, NativeSaveFactory);
// Called only after the ordinary writer has validated/scoped its consumed data.
bool capture_submission(NativeWrites&, uint64_t operation, std::string_view directory);
// Exact synchronous mode1 native checkpoint factory, never a newest-write rule.
bool capture_native_checkpoint(NativeWrites&);
} // namespace sentinel::save
