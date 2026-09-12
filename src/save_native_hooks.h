// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "engine_observer.h"
#include "save_submission.h"
namespace sentinel::save {
// Admission policy for the sole validated native composition. Published only
// after every target/trampoline is prepared; the early RootInit gate releases
// only after every route hook has also been enabled.
inline constexpr uint32_t steam_20260818_routes = startup_route | collector_route |
    campaign_routes | profile_route | provider_identity_route | metadata_route;
// Worker/bootstrap preparation, never DllMain or an IPC dispatch operation.
void configure_prelaunch();
bool owner_retained();
// Production read-only helper/import validation, shared with the controlled host.
bool validate_native_helpers(Installation&, engine::Memory&, const engine::Image&, uintptr_t& initializer);
// Called by the existing hook owner after MinHook initialization/module pinning.
// Save detours never take that owner's installer lock or use its accepting flag.
void install_native_hooks(const engine::Binding&, HANDLE stop);
// Called only after the native runtime has claimed and admitted a save request.
SubmissionResult submit_native_backup(const std::shared_ptr<BackupJob>&, std::string_view directory);
}
