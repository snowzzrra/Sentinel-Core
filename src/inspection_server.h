#pragma once
#include "sentinel_inspection.h"
#include <windows.h>

namespace sentinel {
// Only Core's serialized explicit lifecycle calls start/stop; no DllMain work.
DWORD start_inspection();
DWORD stop_inspection();
Snapshot current_snapshot();
sc_engine_snapshot current_engine_snapshot();
void publish_engine(const sc_engine_snapshot& snapshot);
void inspection_failed(DWORD error);
}
