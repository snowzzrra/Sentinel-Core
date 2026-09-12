// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_storage.h"
#include "engine_observer.h"

namespace sentinel::save {
class Session;
// Supported native RemoteStorage interface, acquired for the SAME HSteamUser
// as the game context. Never a physical-filesystem or foreign-user adapter.
constexpr const char* recovery_provider = "Steam/782330/ISteamRemoteStorage014";
using SteamOwner = uint64_t (*)(uintptr_t remote);
uint64_t current_steam_owner(uintptr_t remote);
struct RecoveryTransport {
    uintptr_t remote = 0;
    SteamOwner owner = nullptr;
    bool (*write)(uintptr_t,const char*,const void*,int32_t) = nullptr;
    int32_t (*read)(uintptr_t,const char*,void*,int32_t) = nullptr;
    bool (*exists)(uintptr_t,const char*) = nullptr;
    int32_t (*size)(uintptr_t,const char*) = nullptr;
    int32_t (*count)(uintptr_t) = nullptr;
    const char* (*name)(uintptr_t,int32_t,int32_t*) = nullptr;
};
enum class RecoveryState { missing_incomplete, same, older, newer, corrupt_unknown, conflict, unavailable };
struct RecoveryResult {
    bool complete = false, mutated = false;
    unsigned preserved_auxiliaries = 0;
    RecoveryState state = RecoveryState::unavailable;
    const char* reason = "not_requested";
    std::wstring quarantine;
};
// Pending without an exact completion remains a durable admission barrier.
bool recovery_clear(storage::Namespace&);
RecoveryResult recover_campaign(storage::Namespace&, const storage::Descriptor&, const RecoveryTransport&);
}
