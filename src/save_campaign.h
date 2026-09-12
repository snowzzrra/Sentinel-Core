// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_storage.h"
#include "save_sdk_write.h"
#include "save_b_trace.h"
#include <mutex>
#include <optional>

namespace sentinel::save {
class Session;
// Facts captured by the native lifecycle owner, never by diagnostic publication.
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
    ParserObservation parser_observation;
    bool enabled = false, resumed = false, source_verified = false, parser_completed = false;
    bool native_saved = false, readback_verified = false, continuity_persisted = false, map_active = false, native_factory_matched = false;
    uint32_t difficulty = 4, effective_difficulty = 4, loaded_difficulty = 4, changes_blocked = 0, parser_result = 0;
    uint64_t operation = 0, checkpoint = 0, source_checkpoint = 0, generation_before = 0, generation_after = 0;
    std::string phase = "disabled", reason = "none", slot, map;
    CampaignTransition transition, checkpoint_boundary;
    uint64_t failure_at_ms=0;
    bool save_ready=false;
};
// One deliberate native UI operation in one fresh process. No inspection API
// initiates gameplay. Durable ownership/options and payload hashes survive PID
// changes; native pointers never do. Normal autosaves use their exact provider ID.
class Campaign final {
public:
    bool configure(Session&, const storage::Descriptor&, storage::Namespace&);
    bool enabled() const;
    bool begin_create(bool clean_native_history, const std::string& slot, int32_t index, bool prospective);
    bool begin_resume();
    bool start_internal(uint32_t difficulty, bool extra_life);
    bool allow_difficulty(uint32_t difficulty);
    bool allow_access(uintptr_t data, const std::string& directory, bool write, bool erase);
    bool write_started(uint64_t operation, const std::string& directory, bool native_factory_matched);
    void write_observed(uint64_t operation, bool terminal, bool successful, engine::Memory&);
    bool verify_source(engine::Memory&, uintptr_t data, uintptr_t image);
    bool parser_enter(uintptr_t data, bool metadata_only = false);
    void observe_parser(ParserObservation, bool metadata_only = false);
    void parser_leave(uint32_t result, bool metadata_only = false);
    bool map_begin(std::string map, uint64_t generation, uint64_t event_id=0);
    bool menu_begin();
    void map_end(const CampaignTransition&);
    bool checkpoint_ready(const CampaignTransition&);
    void refuse(const char* reason,BStage stage=BStage::creation);
    CampaignSnapshot snapshot() const;
private:
    bool reject(const char*,std::initializer_list<BFact> facts={});
    bool save_record(const std::string&, bool create);
    void persist_checkpoint(const SdkWriteObservation&,engine::Memory&);
    void complete_map();
    bool parse_checkpoint(std::string_view);
    std::string checkpoint_text(const SdkWriteObservation&) const;
    mutable std::recursive_mutex mutex_;
    Session* owner_ = nullptr; storage::Namespace* lease_ = nullptr;
    storage::CampaignOptions options_;
    CampaignSnapshot state_;
    BStage diagnostic_stage_=BStage::creation;
    std::string contract_, directory_;
    std::vector<SdkFileWrite> expected_;
    uintptr_t load_data_ = 0;
    uintptr_t metadata_data_ = 0;
    bool metadata_verified_ = false;
    bool contract_created_ = false, checkpoint_exists_ = false, initiated_ = false, map_pending_ = false;
    std::optional<SdkWriteObservation> checkpoint_awaiting_transition_;
};
}
