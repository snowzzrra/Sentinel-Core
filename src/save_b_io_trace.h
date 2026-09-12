// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_b_trace.h"
#include "engine_observer.h"

namespace sentinel::save {
// A guarded read reports the failing field and the underlying memory error,
// without retrying a read or retaining the native bytes in diagnostics.
struct BIoTrace {
    BTrace* trace; BStage stage; uint64_t operation = 0; uintptr_t source = 0;
    BStatus failure_status = BStatus::refused;
    bool check(bool valid, const char* predicate, std::initializer_list<BFact> facts = {}) const {
        if (!valid && trace) trace->record(stage, failure_status, predicate, operation, facts, source);
        return valid;
    }
    bool copy(engine::Memory& memory, uintptr_t address, void* out, size_t size, const char* predicate) const {
        const auto result = memory.copy(address, out, size);
        return check(!result.reason, predicate, {{"memory_reason", result.reason}, {"win32_error", result.error},
            {"bytes", size}, {"source_offset", source && address >= source && address - source <= 0x1000000 ?
                static_cast<int64_t>(address - source) : -1}});
    }
    template<class T> bool read(engine::Memory& memory, uintptr_t base, size_t offset, T& out, const char* predicate) const {
        return check(base && base <= UINTPTR_MAX - offset, predicate, {{"null_source", !base}, {"offset", offset}}) &&
            copy(memory, base + offset, &out, sizeof(out), predicate);
    }
};
}
