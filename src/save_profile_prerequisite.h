// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_catalog.h"

namespace sentinel::save {
using AllocateNative = void* (*)(size_t);
using ConstructSaveData = void* (*)(void*);
using DestroySaveData = void (*)(uintptr_t);
struct ProfilePrerequisiteCalls {
    AllocateNative allocate;
    ConstructSaveData construct;
    DestroySaveData destroy;
    CatalogCalls catalog;
    CreateWrite read;
};
SaveFuture** profile_read_prerequisite(Session&, engine::Memory&, uintptr_t provider, SaveFuture**,
    uintptr_t identity, SaveReference*, const ProfilePrerequisiteCalls&);
} // namespace sentinel::save
