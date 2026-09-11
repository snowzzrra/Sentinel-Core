// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_session.h"
#include "engine_observer.h"
#include "save_write.h"

namespace sentinel::save {
using InitializeSteamContext = uintptr_t (*)(uintptr_t);
struct ProviderCalls { uintptr_t image_base; InitializeSteamContext context; };
using QueryExists = SaveFuture** (*)(SaveFuture**, SaveReference*, const char*);
struct ExistenceCalls { ProviderCalls provider; QueryExists query; ReleaseSaveReference release; };
// Backend runs after the native identity future resolves and consumes that
// identity reference. Results describe file presence, never save completion.
SaveFuture** query_exists_scoped(Session&, engine::Memory&, SaveFuture**, SaveReference*, const char*, const ExistenceCalls&);

// Called after the native initializer, only for the selected root's manager.
// Reads full identity from that actual RemoteStorage root. An empty root may
// receive its own marker under the retained local mutex; existing data without
// that marker refuses. This is namespace preparation, not creation of a game save.
bool provider_initialized(Session&, engine::Memory&, uintptr_t manager, const ProviderCalls&);
// RootInit publishes the selected manager after its constructor initialized the
// provider. Called only after normal RootInit return, before startup_leave.
bool bind_root_provider(Session&, engine::Memory&, const ProviderCalls&);
// Re-read this bound provider's physical inventory and full ownership record.
// This read-only observation never creates or repairs a missing marker.
bool read_native_catalog(Session&, engine::Memory&, std::string_view prefix, NativeCampaignCatalog&, uintptr_t& remote);
} // namespace sentinel::save
