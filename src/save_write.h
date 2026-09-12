// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_session.h"
#include "engine_observer.h"

namespace sentinel::save {
struct SaveReference { uintptr_t control; };
struct SaveResult { int64_t state, outcome; uint32_t value, padding; };
struct SaveFuture;
struct SaveFutureVtable {
    SaveFuture* (*destroy)(SaveFuture*, uint32_t);
    SaveResult* (*poll)(SaveFuture*, SaveResult*, void*);
};
struct SaveFuture { const SaveFutureVtable* vtable; };
// Native preflight returns a moved file vector or an error variant; this is not
// the later bool provider-future result and contains no completion receipt.
struct WritePreflightResult { uint64_t tag, first, second, third; };
using WritePreflight = WritePreflightResult* (*)(uintptr_t, WritePreflightResult*);
WritePreflightResult* preflight_scoped(Session&, engine::Memory&, uintptr_t,
    WritePreflightResult*, WritePreflight, uintptr_t image);
using CreateWrite = SaveFuture** (*)(uintptr_t, SaveFuture**, uintptr_t, SaveReference*);
using SetSaveName = void (*)(uintptr_t, const char*);
using ReleaseSaveReference = void (*)(SaveReference*);
using CheckProfilePayload = bool (*)(Session&, engine::Memory&, uintptr_t);
struct ReadbackCalls;
struct WriteCalls {
    CreateWrite create; SetSaveName set_name; ReleaseSaveReference release; CheckProfilePayload profile;
    uintptr_t image_base = 0;
    CreateWrite profile_read = nullptr;
    const ReadbackCalls* readback = nullptr;
};

// This is a failed provider future, not a failed publication continuation. The
// original caller still moves its lock, creates its task, publishes manager+0x50
// and runs its finalizer after consuming the result. No job or receipt is made.
SaveFuture* refused_save_future();
SaveFuture** write_scoped(Session&, engine::Memory&, uintptr_t provider,
    SaveFuture**, uintptr_t identity, SaveReference*, const WriteCalls&, bool native_checkpoint = false);
SaveFuture** read_scoped(Session&, engine::Memory&, uintptr_t provider,
    SaveFuture**, uintptr_t identity, SaveReference*, const WriteCalls&);
SaveFuture** delete_scoped(Session&, engine::Memory&, uintptr_t provider,
    SaveFuture**, uintptr_t identity, SaveReference*, const WriteCalls&, bool auxiliary);
} // namespace sentinel::save
