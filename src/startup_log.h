#pragma once
#include "sentinel_inspection.h"
namespace sentinel::startup_log {
// Diagnostic observer only, never DllMain or a hook callback. History rotates
// at 1 MiB (current + previous); Special events drain by sequence.
void record(const Snapshot&, uint32_t engine_reason = 0) noexcept;
}
