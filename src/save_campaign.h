// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#pragma once
#include "save_storage.h"
#include "save_sdk_write.h"
#include <mutex>

namespace sentinel::save {
class Session;
struct CampaignSnapshot {
    bool enabled = false, resumed = false, source_verified = false, parser_completed = false;
    bool native_saved = false, readback_verified = false, continuity_persisted = false, map_active = false, native_factory_matched = false;
    uint32_t difficulty = 4, effective_difficulty = 4, loaded_difficulty = 4, changes_blocked = 0, parser_result = 0;
    uint64_t operation = 0, checkpoint = 0, source_checkpoint = 0, generation_before = 0, generation_after = 0;
    std::string phase = "disabled", reason = "none", slot, map;
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
    bool parser_enter(uintptr_t data);
    void parser_leave(uint32_t result);
    bool map_begin(std::string map, uint64_t generation);
    void map_end(bool success, uint64_t generation, uint32_t effective_difficulty);
    void refuse(const char* reason);
    CampaignSnapshot snapshot() const;
private:
    bool reject(const char*);
    bool save_record(const std::string&, bool create);
    bool parse_checkpoint(std::string_view);
    std::string checkpoint_text(const SdkWriteObservation&) const;
    mutable std::recursive_mutex mutex_;
    Session* owner_ = nullptr; storage::Namespace* lease_ = nullptr;
    storage::CampaignOptions options_;
    CampaignSnapshot state_;
    std::string contract_, directory_;
    std::vector<SdkFileWrite> expected_;
    uintptr_t load_data_ = 0;
    bool contract_created_ = false, checkpoint_exists_ = false, initiated_ = false, map_pending_ = false;
};
}
