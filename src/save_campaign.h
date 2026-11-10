// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_storage.h"
#include "save_sdk_write.h"
#include "save_b_trace.h"
#include <mutex>
#include <optional>

namespace sentinel::save {
std::string campaign_contract(const storage::Descriptor&, const std::string& namespace_id);
class Session;
struct ProfileWrite;
enum class ProfilePublication { refused, persisted, checkpoint_owned };
// facts captured by the native lifecycle owner, never by diagnostic publication
struct CampaignTransition {
    uint64_t event_id=0, generation_before=0, generation_after=0, native_return=0, at_ms=0;
    uint32_t game=UINT32_MAX, depth=0, observation_reason=0, difficulty=UINT32_MAX;
    bool observed=false, abnormal=false, state_read=false, map_read=false, difficulty_read=false, campaign=false, ended=false;
    std::array<char,192> map{};
};
struct ParserObservation {
    uint64_t at_ms=0;
    uintptr_t caller=0, data=0;
    std::string directory, prefix, source="unreadable", disposition="not_observed";
    uint32_t session_state=0;
    bool directory_read=false, prefix_read=false, native_completion=false, exact_resume=false;
};
struct CampaignSnapshot {
    uint64_t restart_sequence=0;
    uint32_t restart_status=0, restart_stage_id=0;
    std::string restart_map, restart_reason;
    bool restart_pending=false, restart_enabled=false;
    ParserObservation parser_observation;
    bool enabled = false, resumed = false, source_verified = false, parser_completed = false;
    bool native_saved = false, readback_verified = false, continuity_persisted = false, map_active = false, native_factory_matched = false;
    uint32_t difficulty = 4, effective_difficulty = 4, loaded_difficulty = 4, changes_blocked = 0, parser_result = 0;
    uint64_t operation = 0, checkpoint = 0, source_checkpoint = 0, generation_before = 0, generation_after = 0;
    std::string phase = "disabled", reason = "none", slot, map, end_summary_map;
    CampaignTransition transition, checkpoint_boundary;
    uint64_t failure_at_ms=0;
    bool save_ready=false;
    uint32_t native_subtype=1;
    uint32_t stage_id=0, end_summary_stage_id=0;
    bool archive_complete=false;
    uint64_t archive_bytes=0, archive_copy_us=0;
};
// One deliberate native UI operation in one fresh process. No inspection API
// initiates gameplay. Durable ownership/options and payload hashes survive PID
// changes; native pointers never do. Normal autosaves use their exact provider ID.
class Campaign final {
public:
    bool configure(Session&, const storage::Descriptor&, storage::Namespace&);
    bool refresh_checkpoint();
    bool enabled() const;
    bool begin_create(bool clean_native_history, const std::string& slot, int32_t index, bool prospective);
    bool begin_resume(const std::string& mission_destination = {}, uint32_t stage_id=0, uint32_t variant=2);
    bool prepare_menu_save(const CampaignTransition&);
    void cancel_menu_save();
    bool restoring_mission_checkpoint() const;
    bool final_death(uint32_t stage_id);
    bool begin_restart();
    void restart_failed(const char* reason);
    void restart_tick(uint64_t now);
    bool start_internal(uint32_t difficulty, bool extra_life);
    bool allow_difficulty(uint32_t difficulty);
    bool allow_access(uintptr_t data, const std::string& directory, bool write, bool erase);
    bool write_started(uint64_t operation, const std::string& directory, bool native_factory_matched, uint64_t steam_user);
    std::vector<SdkFileWrite> readback_baseline(uint64_t operation) const;
    void write_observed(uint64_t operation, bool terminal, bool successful, engine::Memory&);
    ProfilePublication finish_profile_write(Session&, const ProfileWrite&, engine::Memory&);
    bool verify_source(engine::Memory&, uintptr_t data, uintptr_t image);
    bool parser_enter(uintptr_t data, bool metadata_only = false);
    void observe_parser(ParserObservation, bool metadata_only = false);
    void parser_leave(uint32_t result, bool metadata_only = false);
    bool map_begin(std::string map, uint64_t generation, uint64_t event_id=0, uint32_t native_subtype=1, uint32_t stage_id=0);
    bool menu_begin();
    void map_end(const CampaignTransition&);
    bool checkpoint_ready(const CampaignTransition&);
    void refuse(const char* reason,BStage stage=BStage::creation);
    CampaignSnapshot snapshot() const;
    bool backup_continuity(const SdkWriteObservation&, storage::TransportMetadata&) const;
private:
    bool allow_campaign_write();
    bool menu_save_available(bool catalog, bool persisted, bool fortress) const;
    bool hydrate_catalog_access(uintptr_t data, const std::string& directory, bool write, bool erase);
    bool verify_source_file(engine::Memory&, uintptr_t files, int32_t index, int32_t count, uintptr_t data, uintptr_t image,
                            bool& backup_group, std::vector<std::string>& seen, bool& checkpoint_changed);
    bool read_source_stream(engine::Memory&, uintptr_t files, int32_t index, uintptr_t image, uintptr_t& file, std::string& name);
    bool digest_source_stream(engine::Memory&, uintptr_t file, int32_t index, uintptr_t data, const SdkFileWrite&,
                              bool backup_group, bool& checkpoint_changed);
    bool menu_checkpoint_ready(const CampaignTransition&);
    bool admit_generation(uint64_t generation,bool mission_launch,bool continuing);
    bool admit_resume_map(const std::string& map,bool continuing,uint32_t stage_id,uint32_t native_subtype);
    bool read_restart();
    bool confirm_restart();
    bool persist_restart();
    bool resume_slot_matches();
    bool resume_source_available() const;
    bool configure_resume(std::string record, storage::Result);
    void observe_receipt(const sc_save_write_snapshot&, uint64_t operation, bool terminal, bool successful);
    enum class MenuSaveSource { none, cold_catalog, persisted_gameplay, active_fortress };
    bool reject(const char*,std::initializer_list<BFact> facts={});
    void persist_checkpoint(const SdkWriteObservation&,engine::Memory&);
    void complete_map();
    bool parse_checkpoint(std::string_view);
    std::string checkpoint_text(const SdkWriteObservation&) const;
    mutable std::recursive_mutex mutex_;
    Session* owner_ = nullptr; storage::Namespace* lease_ = nullptr;
    storage::CampaignOptions options_;
    storage::Descriptor descriptor_;
    storage::CheckpointWrite checkpoint_write_;
    CampaignSnapshot state_;
    BStage diagnostic_stage_=BStage::creation;
    std::string contract_, directory_, mission_destination_, prior_map_;
    uint32_t prior_stage_id_=0;
    std::vector<SdkFileWrite> expected_;
    uintptr_t load_data_ = 0;
    uintptr_t metadata_data_ = 0;
    bool metadata_verified_ = false;
    bool catalog_hydrated_ = false;
    bool menu_active_ = false;
    bool menu_save_pending_ = false;
    MenuSaveSource menu_save_source_ = MenuSaveSource::none;
    uint64_t menu_save_generation_ = 0;
    bool contract_created_ = false, checkpoint_exists_ = false, initiated_ = false, map_pending_ = false;
    std::optional<SdkWriteObservation> checkpoint_awaiting_transition_;
    std::string restart_record_;
    uint64_t restart_checkpoint_=0;
    bool restart_loading_=false;
    uint64_t restart_deadline_=0;
};
}
