// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_session.h"
#include "engine_observer.h"

namespace sentinel::save {
struct DeleteFuture { uintptr_t control; };
struct DeleteResult { int64_t state, outcome; uint32_t error, padding; };
using PollDelete = DeleteResult* (*)(DeleteFuture*, DeleteResult*, void*);
using ReleaseDelete = void (*)(DeleteFuture*);
struct DeleteCalls { PollDelete poll; ReleaseDelete release; };
DeleteResult* refuse_unscoped_delete(Session&, engine::Memory&, DeleteFuture*, DeleteResult*, void*, const DeleteCalls&);
// Gate the directory worker before its first enqueue. Existing jobs retain the
// native polling/release contract; a later fault cannot establish nonexecution.
DeleteResult* poll_scoped_delete(Session&, engine::Memory&, DeleteFuture*, DeleteResult*, void*, const DeleteCalls&);
// Operation result inside the unchanged native job's ownership and cleanup.
struct DeleteOperationResult { int64_t outcome; uint64_t value; };
using DeleteOperation = DeleteOperationResult* (*)(uintptr_t, DeleteOperationResult*);
DeleteOperationResult* delete_auxiliary_scoped(Session&, engine::Memory&, uintptr_t,
    DeleteOperationResult*, DeleteOperation);
} // namespace sentinel::save
