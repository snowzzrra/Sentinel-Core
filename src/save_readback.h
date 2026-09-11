// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_profile_prerequisite.h"

namespace sentinel::save {
struct ReadbackCalls {
    ProfilePrerequisiteCalls native;
    void* (*construct_stream)(void*, const char*, uint32_t);
};
// Capture the borrowed identity in an unpolled native Load future while the
// writer's caller still owns it. Streams are populated only after SDK success.
SaveFuture* create_write_readback(Session&, uint64_t operation, uintptr_t provider,
    uintptr_t identity, const std::string& directory, const ReadbackCalls&);
struct ReadWorkerResult { int64_t outcome; uint32_t value, padding; };
using PrepareRead = ReadWorkerResult* (*)(uintptr_t, ReadWorkerResult*, SaveReference*);
using DecodeRead = ReadWorkerResult* (*)(uintptr_t, ReadWorkerResult*);
ReadWorkerResult* prepare_readback(Session&, engine::Memory&, uintptr_t context,
    ReadWorkerResult*, SaveReference* weak_waiter, PrepareRead);
ReadWorkerResult* verify_readback(Session&, engine::Memory&, uintptr_t context,
    ReadWorkerResult*, DecodeRead, uintptr_t image);
} // namespace sentinel::save
