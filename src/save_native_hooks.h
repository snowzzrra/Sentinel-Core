// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "engine_observer.h"
#include "save_submission.h"
namespace sentinel::save {
// Worker/bootstrap preparation, never DllMain or an IPC dispatch operation.
void configure_prelaunch();
bool owner_retained();
// Called by the existing hook owner after MinHook initialization/module pinning.
// Save detours never take that owner's installer lock or use its accepting flag.
void install_native_hooks(const engine::Binding&, HANDLE stop);
// Called only after the native runtime has claimed and admitted a save request.
SubmissionResult submit_native_backup(const std::shared_ptr<BackupJob>&, std::string_view directory);
}
