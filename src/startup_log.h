#pragma once
#include "sentinel_inspection.h"
namespace sentinel::startup_log {
// Serialized owner/observer only, never DllMain or a hook callback. At most 128
// records per process; writes only when retained installation/session facts change.
void record(const Snapshot&, uint32_t engine_reason = 0) noexcept;
}
