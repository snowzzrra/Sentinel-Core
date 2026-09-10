#pragma once
#include "native_model.h"
#include "engine_observer.h"
#include "sentinel_inspection.h"

namespace sentinel::native {
void prepare(const Snapshot& identity);
void start(const engine::Binding& binding, const Snapshot& identity, HANDLE stop);
uint64_t observation_stamp();
void publish_context(const sc_context_snapshot& snapshot, uint64_t before);
sc_native_snapshot inspect(uint64_t after_event = 0);
sc_diagnostic_result submit(const sc_diagnostic_request& request, sc_diagnostic_detail* detail = nullptr);
sc_diagnostic_result result(const sc_diagnostic_request& request, bool cancel, sc_diagnostic_detail* detail = nullptr);
// False means no native hook was ever enabled/pinned and normal unload remains
// possible. True permanently requires retaining this Core instance to exit.
bool stop();
bool retained();
}
