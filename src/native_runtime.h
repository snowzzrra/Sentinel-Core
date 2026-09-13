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
sc_save_backup_snapshot submit_backup(const sc_save_backup_request&);
sc_save_backup_snapshot backup_result(const sc_save_backup_request&, bool cancel);
sc_weapon_points_result submit_weapon_points(const sc_weapon_points_request&);
sc_weapon_points_result weapon_points_result(const sc_weapon_points_request&, bool cancel, bool release = false);
// False means no native hook was ever enabled/pinned and normal unload remains
// possible. True permanently requires retaining this Core instance to exit.
bool stop();
bool retained();
save::CampaignTransition checkpoint_transition();
}
