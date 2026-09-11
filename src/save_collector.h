// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "engine_observer.h"
#include "save_session.h"

namespace sentinel::save {
struct NativeString {
    uintptr_t vtable;
    char* data;
    int32_t length;
    uint32_t capacity_flags;
    unsigned char storage[24];
};
struct CollectorEntry { NativeString name, read_key; uint32_t size, padding; };
struct CollectorVector { CollectorEntry* entries; uint64_t count, capacity; };
struct CollectorResult { int64_t tag; CollectorVector files; };
struct CollectorContext { uintptr_t provider; NativeString prefix, root; };
static_assert(sizeof(NativeString) == 0x30 && sizeof(CollectorEntry) == 0x68);
static_assert(sizeof(CollectorVector) == 0x18 && sizeof(CollectorResult) == 0x20);
static_assert(offsetof(CollectorContext, root) == 0x38);
using Collect = CollectorResult* (*)(const CollectorContext*, CollectorResult*);
using AssignString = void (*)(NativeString*, const char*);
using ReleaseVector = void (*)(CollectorVector*);
struct CollectorCalls { Collect collect; AssignString assign; ReleaseVector release; };
struct SaveReference;
struct EnumerationFuture;
using CreateEnumeration = EnumerationFuture** (*)(EnumerationFuture**, SaveReference*, const char*, const char*);
EnumerationFuture** enumerate_scoped(Session&, engine::Memory&, EnumerationFuture**,
    SaveReference*, const char* root, const char* prefix, CreateEnumeration);
// Called by the collector detour, inside the native worker's retained-owner
// interval. Native callbacks are the validated originals, never IPC addresses.
CollectorResult* collect_scoped(Session&, engine::Memory&, const CollectorContext*,
                                CollectorResult*, const CollectorCalls&);
} // namespace sentinel::save
