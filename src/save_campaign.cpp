// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_campaign.h"
#include "campaign_stages_generated.h"
#include "save_session.h"
#include "save_catalog.h"
#include "save_provider.h"
#include "save_backup.h"
#include "sentinel_context.h"
#include <windows.h>
#include <algorithm>
#include <cstring>

namespace sentinel::save {
namespace {
template<class T> bool read(engine::Memory& m, uintptr_t p, size_t off, T& value) {
    uintptr_t at = 0; return engine::add(p, off, sizeof(value), at) && !m.copy(at, &value, sizeof(value)).reason;
}
bool text(engine::Memory& m, uintptr_t p, size_t offset, std::string& value) {
    NativeString s{}; std::array<char,260> bytes{};
    if (!read(m,p,offset,s) || !s.data || s.length <= 0 || s.length >= 260 ||
        m.copy(reinterpret_cast<uintptr_t>(s.data),bytes.data(),static_cast<size_t>(s.length)+1).reason ||
        bytes[s.length] || std::memchr(bytes.data(),0,static_cast<size_t>(s.length))) return false;
    value.assign(bytes.data(),static_cast<size_t>(s.length)); return true;
}
std::string hex(const std::array<unsigned char,32>& bytes) {
    constexpr char chars[]="0123456789abcdef"; std::string out;
    for (auto c:bytes) { out+=chars[c>>4]; out+=chars[c&15]; } return out;
}
uint32_t default_stage(const std::string& map,uint32_t native_subtype) {
    uint32_t stage_id=0;
    if (native_subtype!=6) {
        if (map=="game/hub/hub") stage_id=1;
        for (const auto& route:campaign_menu::stage_routes)
            if (route.variant!=6 && map==route.map) { stage_id=route.id; break; }
    }
    return stage_id;
}
uint32_t phase_id(std::string_view phase) {
    constexpr std::string_view names[]={"disabled","armed","create_requested","native_start_queued","resume_requested",
        "source_verified","parser_succeeded","native_transition_pending","native_created","reopened","native_save_pending","checkpoint_saved","refused"};
    for (uint32_t i=0;i<std::size(names);++i) if (phase==names[i]) return i;
    return UINT32_MAX;
}
}
std::string campaign_contract(const storage::Descriptor& descriptor,const std::string& namespace_id) {
    return storage::checkpoint_contract(descriptor,namespace_id);
}
bool Campaign::configure_resume(std::string record, storage::Result r) {
    if (!r.ok() || record != contract_)
        return reject("resume_contract_or_options_mismatch", {{"ok", r.ok()},
                                                              {"outcome", r.outcome},
                                                              {"win32", r.win32_error},
                                                              {"bytes", record.size()},
                                                              {"expected_bytes", contract_.size()},
                                                              {"contract_equal", record == contract_}});
    contract_created_ = true;
    owner_->btrace.record(diagnostic_stage_, BStatus::entered, "resume_checkpoint_read");
    r = lease_->campaign_record(true, record);
    if (!r.ok())
        return reject("resume_checkpoint_read_failed",
                      {{"outcome", r.outcome}, {"win32", r.win32_error}, {"bytes", record.size()}});
    if (record == "sentinel-checkpoint-ab-v1\n") {
        storage::CheckpointPlan plan;
        r = lease_->checkpoint_plan(descriptor_, plan);
        if (!r.ok() || plan.state != storage::CheckpointState::confirmed) return reject("checkpoint_recovery_required");
        record = plan.text;
    }
    if (!parse_checkpoint(record)) return false;
    checkpoint_exists_ = true;
    state_.source_checkpoint = state_.checkpoint;
    return true;
}
bool Campaign::configure(Session& owner,const storage::Descriptor& descriptor,storage::Namespace& lease) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (descriptor.campaign.intent==storage::CampaignIntent::none) return true;
    owner.btrace.record(BStage::creation,BStatus::entered,"campaign_configure",0,
        {{"intent",descriptor.campaign.intent},{"difficulty",descriptor.campaign.difficulty},{"already_configured",owner_!=nullptr}});
    if (descriptor.campaign.difficulty>3 || owner_ || (descriptor.campaign.restart_stage && descriptor.campaign.difficulty!=3)) {
        owner.btrace.record(BStage::creation,BStatus::refused,"campaign_configuration_invalid",0,
            {{"difficulty",descriptor.campaign.difficulty},{"already_configured",owner_!=nullptr}}); return false;
    }
    owner_=&owner; lease_=&lease; options_=descriptor.campaign; descriptor_=descriptor; state_.enabled=true;
    state_.difficulty=options_.difficulty; state_.resumed=options_.intent==storage::CampaignIntent::resume;
    state_.restart_enabled=options_.restart_stage;
    // exact native suffix, never an index inferred from directory enumeration
    state_.slot="AUTOSAVE0"; directory_=owner.native_root()+"/GAME-"+state_.slot;
    contract_=campaign_contract(descriptor,owner.namespace_id());
    diagnostic_stage_=state_.resumed?BStage::resume:BStage::creation;
    owner.btrace.record(diagnostic_stage_,BStatus::entered,"campaign_contract_read");
    std::string record; auto r=lease.campaign_record(false,record);
    owner.btrace.record(diagnostic_stage_,BStatus::succeeded,"campaign_contract_read_returned",0,
        {{"ok",r.ok()},{"outcome",r.outcome},{"win32",r.win32_error},{"bytes",record.size()},{"expected_bytes",contract_.size()},{"contract_equal",record==contract_}});
    if (!state_.resumed) {
        if (r.ok() || r.win32_error!=ERROR_FILE_NOT_FOUND) return reject("create_requires_unreserved_namespace",{{"ok",r.ok()},{"outcome",r.outcome},{"win32",r.win32_error}});
        owner.btrace.record(diagnostic_stage_,BStatus::entered,"create_checkpoint_absence_read");
        r=lease.campaign_record(true,record);
        if (r.ok() || r.win32_error!=ERROR_FILE_NOT_FOUND) return reject("create_has_checkpoint_or_partial_metadata",{{"ok",r.ok()},{"outcome",r.outcome},{"win32",r.win32_error},{"bytes",record.size()}});
    } else {
        if (!configure_resume(record, r)) return false;
    }
    if (options_.restart_stage && !read_restart()) return false;
    state_.phase="armed";
    owner.btrace.record(diagnostic_stage_,BStatus::succeeded,"campaign_armed",0,
        {{"resumed",state_.resumed},{"checkpoint",state_.checkpoint},{"expected_files",expected_.size()},{"difficulty",state_.difficulty}});
    return true;
}
bool Campaign::enabled() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return state_.enabled; }
bool Campaign::refresh_checkpoint() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    storage::CheckpointPlan plan;
    if (!lease_->checkpoint_plan(descriptor_,plan).ok() || plan.state!=storage::CheckpointState::confirmed || !parse_checkpoint(plan.text))
        return reject("checkpoint_recovery_required");
    if (options_.restart_stage && !read_restart()) return false;
    state_.source_checkpoint=state_.checkpoint; state_.phase="armed"; return true;
}
bool Campaign::restoring_mission_checkpoint() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return state_.enabled && (state_.native_subtype==2 || state_.native_subtype==6) && mission_destination_.empty() && !restart_loading_;
}
CampaignSnapshot Campaign::snapshot() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return state_; }
bool Campaign::reject(const char* reason,std::initializer_list<BFact> facts) {
    if (state_.restart_pending) { state_.restart_status=3; state_.restart_reason=reason; }
    const auto prior=owner_->btrace.snapshot().stages[static_cast<size_t>(diagnostic_stage_)];
    std::array<BFact,16> detail{{{"phase",phase_id(state_.phase)},{"session_state",owner_->state()},{"session_fault",owner_->fault()}}};
    size_t count=3;
    auto append=[&](const BFact& fact) {
        if (!fact.key || count==detail.size()) return;
        for (size_t i=0;i<count;++i) if (std::strcmp(detail[i].key,fact.key)==0) return;
        detail[count++]=fact;
    };
    if (facts.size()) { for (const auto& fact:facts) append(fact); }
    else { for (const auto& fact:prior.facts) append(fact); }
    owner_->btrace.record_fields(diagnostic_stage_,BStatus::refused,reason,state_.operation,detail.data(),count,prior.source);
    if (state_.reason=="none") { state_.reason=reason; state_.failure_at_ms=GetTickCount64(); }
    state_.phase="refused"; owner_->fail(SessionFault::native_campaign); return false;
}
void Campaign::refuse(const char* reason,BStage stage) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state_.enabled) { diagnostic_stage_=stage; reject(reason); }
}
bool Campaign::begin_create(bool clean,const std::string& slot,int32_t index,bool prospective) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    diagnostic_stage_=BStage::creation;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"create_selection_validate",0,
        {{"accepting",owner_->accepts_requests()},{"initiated",initiated_},{"resumed",state_.resumed},{"clean",clean},
         {"prospective",prospective},{"index",index},{"slot_equal",slot==state_.slot},{"phase",phase_id(state_.phase)}});
    if (!owner_->accepts_requests() || initiated_ || state_.resumed || !clean || !prospective || index!=0 || slot!=state_.slot)
        return reject("fresh_process_and_owned_prospective_slot_required",
            {{"accepting",owner_->accepts_requests()},{"initiated",initiated_},{"resumed",state_.resumed},{"clean",clean},
             {"prospective",prospective},{"index",index},{"slot_equal",slot==state_.slot},{"phase",phase_id(state_.phase)}});
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"create_contract_publish",0,{{"bytes",contract_.size()}});
    const auto result=lease_->publish_campaign_record(false,contract_,true);
    if (!result.ok()) return reject("create_contract_reservation_failed",{{"outcome",result.outcome},{"win32",result.win32_error},{"bytes",contract_.size()}});
    contract_created_=true; initiated_=true; state_.phase="create_requested";
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"create_contract_reserved",0,{{"outcome",result.outcome},{"win32",result.win32_error},{"difficulty",options_.difficulty}});
    return true;
}
bool Campaign::prepare_menu_save(const CampaignTransition& boundary) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    // the visible, unlocked native selection supplies the request; the catalog, menu checkpoint, or active fortress supplies its save source
    const bool catalog=state_.resumed && !initiated_ && catalog_hydrated_;
    const bool persisted=menu_active_ && state_.continuity_persisted;
    const bool fortress=state_.map_active && state_.map=="game/hub/hub" &&
        boundary.generation_after==state_.generation_after;
    if (!owner_->accepts_requests() || (!catalog && !persisted && !fortress) || !checkpoint_exists_ || expected_.empty() ||
        map_pending_ || state_.phase=="native_save_pending" || !boundary.observed || boundary.observation_reason ||
        boundary.depth || (!catalog && !boundary.generation_after) || menu_save_pending_) return false;
    initiated_=true; if (!fortress) menu_active_=true; menu_save_pending_=true;
    menu_save_source_=fortress ? MenuSaveSource::active_fortress :
        catalog ? MenuSaveSource::cold_catalog : MenuSaveSource::persisted_gameplay;
    menu_save_generation_=boundary.generation_after;
    owner_->btrace.record(BStage::checkpoint_factory,BStatus::succeeded,"native_mission_presave_reserved",state_.operation,
        {{"generation",menu_save_generation_},{"checkpoint",state_.checkpoint},{"catalog",catalog},{"fortress",fortress}});
    return true;
}
void Campaign::cancel_menu_save() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!menu_save_pending_) return;
    if (menu_save_source_!=MenuSaveSource::active_fortress) state_.save_ready=false;
    if (menu_save_source_==MenuSaveSource::cold_catalog) {
        initiated_=false;
        menu_active_=false;
    }
    menu_save_pending_=false;
    menu_save_source_=MenuSaveSource::none;
    menu_save_generation_=0;
    owner_->btrace.record(BStage::checkpoint_factory,BStatus::pending,"native_mission_presave_cancelled",state_.operation);
}
bool Campaign::resume_slot_matches() {
    ProfileChoice choice{};
    const bool valid=owner_->profile_choice(choice);
    return (valid && choice.name.data()==state_.slot) || reject("resume_selection_mismatch",
        {{"choice_valid",valid},{"slot_equal",choice.name.data()==state_.slot},{"index",choice.index}});
}
bool Campaign::resume_source_available() const {
    const bool first=state_.resumed && !initiated_;
    const bool menu=(menu_active_ || (menu_save_source_==MenuSaveSource::active_fortress &&
        state_.map_active && !menu_save_pending_)) && state_.continuity_persisted && !map_pending_;
    return first || menu;
}
bool Campaign::begin_resume(const std::string& mission_destination,uint32_t stage_id,uint32_t variant) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    if (state_.restart_pending && (!restart_loading_ || mission_destination!=state_.restart_map))
        return reject("restart_required_before_resume");
    diagnostic_stage_=BStage::resume;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"resume_selection_validate",0,
        {{"accepting",owner_->accepts_requests()},{"resumed",state_.resumed},{"initiated",initiated_},
         {"checkpoint_exists",checkpoint_exists_},{"expected_files",expected_.size()}});
    if (menu_save_source_==MenuSaveSource::active_fortress && mission_destination.empty()) {
        owner_->btrace.record(diagnostic_stage_,BStatus::blocked,"mission_destination_missing",state_.operation);
        return false;
    }
    if (!owner_->accepts_requests() || !resume_source_available() || !checkpoint_exists_ || expected_.empty())
        return reject("explicit_resume_source_required",{{"accepting",owner_->accepts_requests()},{"resumed",state_.resumed},
            {"initiated",initiated_},{"checkpoint_exists",checkpoint_exists_},{"expected_files",expected_.size()}});
    if (!resume_slot_matches()) return false;
    initiated_=true; menu_active_=false; state_.resumed=true; state_.phase="resume_requested";
    menu_save_pending_=false;
    if (menu_save_source_!=MenuSaveSource::active_fortress) menu_save_source_=MenuSaveSource::none;
    if (menu_save_source_==MenuSaveSource::active_fortress) {
        state_.map_active=false;
        state_.save_ready=false;
    }
    state_.source_checkpoint=state_.checkpoint;
    load_data_=0; state_.source_verified=false; state_.parser_completed=false;
    metadata_data_=0; metadata_verified_=false; catalog_hydrated_=false;
    mission_destination_=mission_destination;
    if (!mission_destination.empty()) { state_.native_subtype=variant; state_.stage_id=stage_id; }
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"resume_requested",0,{{"source_checkpoint",state_.source_checkpoint},{"expected_files",expected_.size()}});
    return true;
}
bool Campaign::start_internal(uint32_t difficulty,bool extra_life) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    diagnostic_stage_=BStage::creation;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"new_internal_validate",0,
        {{"difficulty",difficulty},{"expected_difficulty",options_.difficulty},{"extra_life",extra_life},{"phase",phase_id(state_.phase)}});
    if (!owner_->accepts_requests() || !initiated_ || state_.resumed || state_.phase!="create_requested" ||
        difficulty!=options_.difficulty || extra_life) return reject("native_creation_options_mismatch",
            {{"accepting",owner_->accepts_requests()},{"initiated",initiated_},{"resumed",state_.resumed},{"phase",phase_id(state_.phase)},
             {"difficulty",difficulty},{"expected_difficulty",options_.difficulty},{"extra_life",extra_life}});
    state_.phase="native_start_queued";
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"native_start_queued",0,{{"difficulty",difficulty}}); return true;
}
bool Campaign::allow_difficulty(uint32_t value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || !initiated_) return true;
    diagnostic_stage_=BStage::difficulty;
    if (state_.resumed && state_.parser_completed && !state_.map_active) state_.loaded_difficulty=value;
    if (value==options_.difficulty) {
        owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"room_difficulty_accepted",state_.operation,
            {{"actual",value},{"expected",options_.difficulty},{"resumed",state_.resumed},{"parser_completed",state_.parser_completed}}); return true;
    }
    if (state_.resumed && state_.parser_completed && !state_.map_active) return reject("loaded_difficulty_contradicts_slot_options",
        {{"actual",value},{"expected",options_.difficulty},{"parser_completed",state_.parser_completed},{"map_active",state_.map_active}});
    if (state_.changes_blocked!=UINT32_MAX) ++state_.changes_blocked;
    owner_->btrace.record(diagnostic_stage_,BStatus::blocked,"room_difficulty_change_blocked",state_.operation,
        {{"actual",value},{"expected",options_.difficulty},{"blocked_count",state_.changes_blocked}});
    return false; // room managed: reject the change at the native setter, no frame spam
}
bool Campaign::allow_access(uintptr_t data,const std::string& directory,bool write,bool erase) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || directory=="PROFILE") return true;
    diagnostic_stage_=write ? BStage::checkpoint_factory : state_.resumed ? BStage::resume : BStage::creation;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"campaign_access",state_.operation,
        {{"write",write},{"erase",erase},{"directory_equal",directory==directory_},{"accepting",owner_->accepts_requests()},
         {"initiated",initiated_},{"resumed",state_.resumed},{"phase",phase_id(state_.phase)},{"source_equal",load_data_==data}},data);
    // Native catalog hydration is read-only, both at startup and after returning
    // to the shell. A previously completed gameplay load is not its owner.
    // It never associates a gameplay load; gameplay parser entry requires the
    // explicit later LoadGameSlot operation and its exact SaveData identity.
    const bool catalog_read=(!initiated_ && state_.phase=="armed") ||
        (menu_active_ && state_.continuity_persisted && !map_pending_);
    if (!write && !erase && catalog_read &&
        owner_->native_io() && directory==directory_) {
        metadata_data_=data; metadata_verified_=false; catalog_hydrated_=false; return true;
    }
    if (!owner_->accepts_requests() || !initiated_ || directory!=directory_ || erase)
        return reject("campaign_access_outside_authorized_slot",{{"accepting",owner_->accepts_requests()},
            {"initiated",initiated_},{"directory_equal",directory==directory_},{"erase",erase},{"write",write}});
    if (write) return state_.save_ready || state_.map_active || reject("campaign_write_before_native_map");
    if (!state_.resumed || state_.phase!="resume_requested" || (load_data_ && load_data_!=data))
        return reject("unassociated_campaign_load",{{"resumed",state_.resumed},{"phase",phase_id(state_.phase)},
            {"source_present",load_data_!=0},{"source_equal",load_data_==data}});
    load_data_=data;
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"campaign_load_associated",state_.operation,{{"source_present",data!=0}},data);
    return true;
}
bool Campaign::write_started(uint64_t operation,const std::string& directory,bool native_factory_matched,uint64_t steam_user) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || directory=="PROFILE") return true;
    diagnostic_stage_=BStage::checkpoint_factory;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"checkpoint_write_associate",operation,
        {{"accepting",owner_->accepts_requests()},{"native_factory_matched",native_factory_matched},{"directory_equal",directory==directory_},
         {"map_active",state_.map_active},{"save_ready",state_.save_ready},{"phase",phase_id(state_.phase)},{"previous_operation",state_.operation}});
    if (!owner_->accepts_requests() || !native_factory_matched || !operation || directory!=directory_ ||
        (!state_.map_active && !state_.save_ready) || state_.phase=="native_save_pending") return reject("save_operation_conflict",
            {{"accepting",owner_->accepts_requests()},{"native_factory_matched",native_factory_matched},{"operation",operation},
             {"directory_equal",directory==directory_},{"map_active",state_.map_active},{"save_ready",state_.save_ready},{"phase",phase_id(state_.phase)}});
    const auto begun=lease_->begin_checkpoint(descriptor_,steam_user,checkpoint_write_);
    if (!begun.ok() || checkpoint_write_.number!=state_.checkpoint+1) return reject("checkpoint_pending_write_failed");
    state_.operation=operation; state_.native_factory_matched=true; state_.native_saved=false; state_.readback_verified=false;
    menu_save_pending_=false;
    if (menu_save_source_!=MenuSaveSource::active_fortress) menu_save_source_=MenuSaveSource::none;
    state_.archive_complete=false; state_.archive_bytes=0; state_.archive_copy_us=0;
    state_.continuity_persisted=false; state_.phase="native_save_pending";
    owner_->btrace.record(BStage::checkpoint_factory,BStatus::succeeded,"checkpoint_write_associated",operation,
        {{"native_factory_matched",native_factory_matched},{"generation",state_.generation_after},{"checkpoint",state_.checkpoint}}); return true;
}
std::vector<SdkFileWrite> Campaign::readback_baseline(uint64_t operation) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || operation!=state_.operation || state_.phase!="native_save_pending") return {};
    // dirty-stream saves need the unchanged files in their readback too
    return expected_;
}
std::string Campaign::checkpoint_text(const SdkWriteObservation& observation) const {
    std::string value=contract_+"checkpoint="+std::to_string(state_.checkpoint)+"\nmap="+state_.map+
        "\nsubtype="+std::to_string(state_.native_subtype)+"\nstage="+std::to_string(state_.stage_id)+
        "\nfiles="+std::to_string(observation.payloads.size())+"\n";
    for (const auto& f:observation.payloads) value+="file="+std::string(f.name.data())+","+std::to_string(f.size)+","+hex(f.sha256)+"\n";
    return value+"state=native_saved_readback_verified\n";
}
bool Campaign::backup_continuity(const SdkWriteObservation& observation, storage::TransportMetadata& out) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // the verified readback can finish before the campaign future observes native completion
    if (!state_.enabled || observation.operation != state_.operation || observation.directory != directory_ ||
        state_.phase != "native_save_pending" || state_.checkpoint == UINT64_MAX)
        return false;
    out.contract = contract_;
    out.checkpoint = checkpoint_text(observation);
    const auto old = "checkpoint=" + std::to_string(state_.checkpoint) + "\n";
    out.checkpoint.replace(contract_.size(), old.size(), "checkpoint=" + std::to_string(state_.checkpoint + 1) + "\n");
    return true;
}
bool Campaign::parse_checkpoint(std::string_view value) {
    diagnostic_stage_=BStage::resume;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"checkpoint_metadata_parse",0,{{"bytes",value.size()},{"contract_bytes",contract_.size()}});
    storage::CheckpointRecord record;
    const auto result = storage::parse_checkpoint(value,contract_,directory_,record);
    if (!result.ok()) return reject(result.outcome==storage::Outcome::interrupted_preparation ?
        "checkpoint_save_interrupted" : "checkpoint_metadata_invalid",{{"outcome",result.outcome}});
    if (!record.stage_id) record.stage_id=default_stage(record.map,record.subtype);
    if (record.stage_id && record.stage_id!=1) {
        const auto route=campaign_menu::stage_route(record.stage_id);
        if (!route || record.map!=route->map || (record.subtype==6)!=(route->variant==6))
            return reject("checkpoint_stage_mismatch");
    }
    state_.checkpoint=record.number; state_.map=record.map; state_.native_subtype=record.subtype;
    state_.stage_id=record.stage_id;
    expected_.clear();
    for (const auto& file:record.files) {
        SdkFileWrite native{}; const auto name=directory_+"/"+file.name;
        std::memcpy(native.name.data(),name.c_str(),name.size()+1);
        native.size=file.size; native.sha256=file.sha256; expected_.push_back(native);
    }
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"checkpoint_metadata_verified",0,{{"checkpoint",state_.checkpoint},{"expected_files",expected_.size()}}); return true;
}
void Campaign::observe_receipt(const sc_save_write_snapshot& actual, uint64_t operation, bool terminal,
                               bool successful) {
    state_.native_saved=actual.native_state==0 && actual.native_outcome==0 && actual.native_value==1 &&
        (actual.flags&SC_SAVE_WRITE_SDK_SUCCEEDED) && (actual.flags&SC_SAVE_WRITE_CALLBACKS_SUCCEEDED);
    state_.readback_verified=(actual.flags&SC_SAVE_WRITE_READBACK_HASHES) && (actual.flags&SC_SAVE_WRITE_READBACK_TERMINAL) && !(actual.flags&SC_SAVE_WRITE_READBACK_ERROR);
    owner_->btrace.record(diagnostic_stage_,terminal?BStatus::entered:BStatus::pending,"checkpoint_native_receipt",operation,
        {{"terminal",terminal},{"successful",successful},{"native_state",actual.native_state},{"native_outcome",actual.native_outcome},
         {"native_value",actual.native_value},{"flags",actual.flags},{"native_saved",state_.native_saved},{"readback_verified",state_.readback_verified},{"map_pending",map_pending_}});
}
void Campaign::write_observed(uint64_t operation, bool terminal, bool successful, engine::Memory& memory) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || operation != state_.operation) return;
    diagnostic_stage_ = BStage::continuity;
    const auto actual = owner_->native_writes.snapshot(operation);
    observe_receipt(actual, operation, terminal, successful);
    if (!terminal) return;
    SdkWriteObservation manifest;
    const bool receipt_ok=successful && state_.native_saved && state_.readback_verified;
    const bool manifest_ok=receipt_ok && owner_->native_writes.readback_manifest(operation,manifest);
    if (!receipt_ok || !manifest_ok || manifest.directory!=directory_ || manifest.payloads.size()>64) {
        reject("native_checkpoint_completion_unproven",{{"successful",successful},{"native_state",actual.native_state},
            {"native_outcome",actual.native_outcome},{"native_value",actual.native_value},{"flags",actual.flags},
            {"native_saved",state_.native_saved},{"readback_verified",state_.readback_verified},{"manifest_observed",receipt_ok},
            {"manifest_valid",manifest_ok},{"directory_equal",manifest.directory==directory_},{"file_count",manifest.payloads.size()}}); return;
    }
    const auto archive=owner_->native_writes.backup(operation);
    const auto progress=archive ? archive->progress() : BackupProgress{};
    state_.archive_complete=archive && progress.state==BackupState::complete;
    state_.archive_bytes=progress.output.bytes; state_.archive_copy_us=progress.copy_us;
    if (!state_.archive_complete) {
        reject("checkpoint_archive_incomplete",{{"archive_attached",archive!=nullptr},{"storage_complete",progress.storage_complete},
            {"failure",progress.failure},{"outcome",progress.storage_result.outcome},{"win32",progress.storage_result.win32_error}}); return;
    }
    if (map_pending_) {
        checkpoint_awaiting_transition_=std::move(manifest);
        owner_->btrace.record(diagnostic_stage_,BStatus::pending,"checkpoint_awaits_transition",operation,
            {{"native_saved",state_.native_saved},{"readback_verified",state_.readback_verified},{"expected_generation",state_.transition.generation_after}}); return;
    }
    persist_checkpoint(manifest,memory);
}
ProfilePublication Campaign::finish_profile_write(Session& owner,const ProfileWrite& write,engine::Memory& memory) {
    // profile can finish before the campaign's complete payload and readback
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state_.enabled && owner_==&owner && owner.accepts_requests() &&
        state_.phase=="native_save_pending" && write.campaign==0 &&
        write.choice.index==0 && steam_name_equal(write.choice.name.data(),state_.slot)) {
        // the campaign receipt owns selection persistence for this operation
        owner.btrace.record(BStage::profile_publish,BStatus::pending,"selection_deferred_to_native_checkpoint",write.sequence,
            {{"checkpoint_operation",state_.operation},{"checkpoint",state_.checkpoint},{"campaign",write.campaign}});
        return ProfilePublication::checkpoint_owned;
    }
    return owner.persist_profile_write(write,memory)?ProfilePublication::persisted:ProfilePublication::refused;
}
void Campaign::persist_checkpoint(const SdkWriteObservation& manifest,engine::Memory& memory) {
    diagnostic_stage_=BStage::continuity;
    const auto archive=owner_->native_writes.backup(state_.operation);
    if (!archive) { reject("checkpoint_archive_incomplete"); return; }
    const auto path=archive->progress().output.path;
    const auto basename=path.substr(path.find_last_of(L"\\/")+1);
    if (!lease_->stage_checkpoint(checkpoint_write_,basename).ok() || !lease_->receipt_checkpoint(checkpoint_write_).ok()) {
        reject("checkpoint_receipt_write_failed"); return;
    }
    ProfileChoice choice{}; ProfileWrite selection{};
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"checkpoint_selection_capture",state_.operation,
        {{"checkpoint_before",state_.checkpoint},{"file_count",manifest.payloads.size()}});
    const bool choice_valid=owner_->profile_choice(choice);
    const bool captured=choice_valid && owner_->capture_profile_write(choice.name.data(),choice.index,0,selection);
    if (!captured) { reject("checkpoint_selection_persistence_failed",{{"choice_valid",choice_valid},{"captured",captured},{"index",choice.index},{"slot_equal",choice.name.data()==state_.slot}}); return; }
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"checkpoint_selection_publish",state_.operation,
        {{"index",choice.index},{"slot_equal",choice.name.data()==state_.slot}});
    if (!owner_->persist_profile_write(selection,memory)) { reject("checkpoint_selection_persistence_failed",
        {{"choice_valid",choice_valid},{"captured",captured},{"persisted",false},{"index",choice.index}}); return; }
    if (!lease_->commit_checkpoint(checkpoint_write_).ok()) { reject("checkpoint_commit_write_failed"); return; }
    state_.checkpoint=checkpoint_write_.number;
    const auto cleanup=lease_->prune_checkpoints();
    owner_->btrace.record(diagnostic_stage_,cleanup.ok()?BStatus::succeeded:BStatus::pending,"checkpoint_retention",state_.operation,
        {{"outcome",cleanup.outcome},{"win32",cleanup.win32_error}});
    expected_=manifest.payloads; checkpoint_exists_=true; state_.continuity_persisted=true; state_.phase="checkpoint_saved";
    if (menu_active_) state_.save_ready=false;
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"checkpoint_continuity_persisted",state_.operation,
        {{"checkpoint",state_.checkpoint},{"file_count",expected_.size()},{"native_saved",state_.native_saved},
         {"readback_verified",state_.readback_verified},{"continuity_persisted",state_.continuity_persisted}});
}
bool Campaign::verify_source(engine::Memory& memory,uintptr_t data,uintptr_t image) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || (data!=load_data_ && data!=metadata_data_)) return true;
    const bool metadata_only=data==metadata_data_;
    diagnostic_stage_=BStage::resume;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"resume_source_validate",state_.operation,
        {{"expected_files",expected_.size()},{"source_checkpoint",state_.source_checkpoint},{"metadata_only",metadata_only}},data);
    uintptr_t files=0; int32_t count=0; std::string directory;
    if (!text(memory,data,0,directory)) return reject("resume_directory_unreadable");
    if (directory!=directory_) return reject("resume_directory_mismatch",{{"directory_equal",false}});
    // Catalog hydration names the owned slot but is not a checkpoint import.
    // It deliberately stops before inspecting the transient file vector: native
    // catalog metadata has no Resume payload contract and cannot certify one.
    if (metadata_only) {
        metadata_verified_=true;
        owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"metadata_catalog_verified",state_.operation,
            {{"metadata_only",true},{"source_verified",state_.source_verified},{"directory_equal",true}},data);
        return true;
    }
    if (!read(memory,data,0x1c0,files)) return reject("resume_file_vector_unreadable");
    if (!read(memory,data,0x1c8,count)) return reject("resume_file_count_unreadable");
    // 1414972b0 requests the primary streams OR their -BACKUP counterparts.
    // A native write receipt contains both groups. Require one complete group,
    // not every persisted version and not an arbitrary matching subset.
    const auto backup=[](std::string_view name) {
        constexpr std::string_view suffix="-BACKUP";
        return name.size()>=suffix.size() && steam_name_equal(name.substr(name.size()-suffix.size()),suffix);
    };
    const auto primary_count=std::count_if(expected_.begin(),expected_.end(),[&](const auto& f){return !backup(f.name.data());});
    if (count<=0 || count!=primary_count) return reject("resume_file_count_mismatch",
        {{"actual_count",count},{"expected_count",primary_count},{"persisted_count",expected_.size()},{"metadata_only",metadata_only}});
    bool backup_group=false;
    bool checkpoint_changed=false;
    std::vector<std::string> seen;
    for (int32_t i=0;i<count;++i) {
        owner_->btrace.record(diagnostic_stage_,BStatus::entered,"resume_payload_validate",state_.operation,{{"file_index",i},{"file_count",count}},data);
        uintptr_t file=0,table=0,buffer=0; uint64_t size=0,capacity=0; std::string name;
        if (!read(memory,files,static_cast<size_t>(i)*8,file)) return reject("resume_file_pointer_unreadable");
        if (!read(memory,file,0,table)) return reject("resume_file_type_unreadable");
        if (table!=image+0x2a575a8) return reject("resume_file_type_mismatch",{{"file_index",i},{"type_equal",false}});
        if (!text(memory,file,8,name)) return reject("resume_file_name_unreadable");
        if (!i) backup_group=backup(name);
        else if (backup(name)!=backup_group) return reject("resume_file_group_mixed",{{"file_index",i},{"backup_group",backup_group}});
        const auto full=directory_+"/"+name;
        const auto expected=std::find_if(expected_.begin(),expected_.end(),[&](const auto& f){return steam_name_equal(f.name.data(),full);});
        std::array<unsigned char,32> hash{};
        if (expected==expected_.end()) return reject("resume_file_not_in_checkpoint",{{"file_index",i},{"expected_count",expected_.size()}});
        if (std::find(seen.begin(),seen.end(),expected->name.data())!=seen.end()) return reject("resume_file_duplicate",{{"file_index",i}});
        if (!read(memory,file,0x150,size)) return reject("resume_file_size_unreadable");
        if (!size || size>100u*1024u*1024u) return reject("resume_file_size_invalid",{{"file_index",i},{"actual_size",size}});
        if (!read(memory,file,0x158,capacity)) return reject("resume_file_capacity_unreadable");
        if (size>capacity) return reject("resume_file_exceeds_capacity",{{"file_index",i},{"size",size},{"capacity",capacity}});
        if (!read(memory,file,0x168,buffer)) return reject("resume_file_buffer_unreadable");
        const auto begin=GetTickCount64(), deadline=begin+1000;
        owner_->btrace.record(diagnostic_stage_,BStatus::entered,"resume_payload_digest",state_.operation,
            {{"file_index",i},{"size",size},{"expected_size",expected->size},{"buffer_present",buffer!=0},{"deadline_ms",deadline}},data);
        if (!digest_payload(memory,buffer,static_cast<uint32_t>(size),hash,deadline,&owner_->btrace,BStage::resume,state_.operation,i)) return reject("resume_payload_digest_failed",
            {{"file_index",i},{"size",size},{"elapsed_ms",GetTickCount64()-begin},{"deadline_elapsed",GetTickCount64()>=deadline},{"buffer_present",buffer!=0}});
        if (size!=expected->size || hash!=expected->sha256) {
            checkpoint_changed=true;
            owner_->btrace.record(diagnostic_stage_,BStatus::pending,"resume_checkpoint_drift",state_.operation,
                {{"file_index",i},{"actual_size",size},{"expected_size",expected->size},{"hash_equal",hash==expected->sha256},{"backup_group",backup_group}},data);
        }
        seen.emplace_back(expected->name.data());
    }
    if (checkpoint_changed) return reject("resume_payload_hash_mismatch");
    state_.source_verified=true; state_.phase="source_verified";
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"resume_source_verified",state_.operation,
        {{"file_count",count},{"expected_count",primary_count},{"persisted_count",expected_.size()},{"backup_group",backup_group},
         {"metadata_only",metadata_only},{"all_hashes_equal",!checkpoint_changed},{"source_checkpoint",state_.source_checkpoint}},data); return true;
}
void Campaign::observe_parser(ParserObservation observation,bool metadata_only) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return;
    diagnostic_stage_=BStage::parser;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"native_parser_observed",state_.operation,
        {{"directory_read",observation.directory_read},{"directory_equal",observation.directory==directory_},
         {"prefix_read",observation.prefix_read},{"prefix_equal",observation.prefix=="GAME-"},{"native_completion",observation.native_completion},
         {"source_equal",load_data_==observation.data},{"source_verified",state_.source_verified},{"resumed",state_.resumed},
         {"session_state",owner_->state()},{"session_fault",owner_->fault()}},observation.data);
    if (state_.parser_observation.at_ms) return;
    observation.at_ms=GetTickCount64(); observation.session_state=static_cast<uint32_t>(owner_->state());
    observation.exact_resume=state_.resumed && state_.source_verified && load_data_==observation.data;
    if (observation.directory_read) observation.source=observation.directory=="PROFILE" ? "shared_profile" :
        observation.directory==directory_ && !directory_.empty() ? "owned_campaign" : "foreign_or_unowned_campaign";
    observation.disposition=(owner_->state()==SessionState::rejected || owner_->state()==SessionState::faulted) ?
        "downstream_of_terminal_session" : observation.exact_resume ? "exact_resume" :
        metadata_only && metadata_verified_ && metadata_data_==observation.data ?
        "verified_metadata_read" : "uncorrelated_campaign_import";
    state_.parser_observation=std::move(observation);
}
bool Campaign::parser_enter(uintptr_t data,bool metadata_only) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    diagnostic_stage_=BStage::parser;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"native_parser_enter",state_.operation,
        {{"accepting",owner_->accepts_requests()},{"resumed",state_.resumed},{"source_verified",state_.source_verified},{"metadata_only",metadata_only},
         {"source_equal",(metadata_only?metadata_data_:load_data_)==data},{"session_state",owner_->state()},{"session_fault",owner_->fault()}},data);
    if (owner_->state()==SessionState::rejected || owner_->state()==SessionState::faulted) {
        owner_->btrace.record(diagnostic_stage_,BStatus::blocked,"native_parser_after_terminal_session",state_.operation,
            {{"session_state",owner_->state()},{"session_fault",owner_->fault()},{"source_equal",load_data_==data}},data); return false;
    }
    if (metadata_only) {
        const bool valid=owner_->native_io() && metadata_data_==data && metadata_verified_;
        metadata_data_=0; metadata_verified_=false;
        return valid || reject("metadata_parser_source_not_correlated",{{"metadata_only",true},{"initiated",initiated_}});
    }
    return (owner_->accepts_requests() && state_.resumed && state_.source_verified && load_data_==data) || reject("load_parser_source_not_correlated",
        {{"metadata_only",false},{"initiated",initiated_},{"source_verified",state_.source_verified},{"source_equal",load_data_==data}});
}
void Campaign::parser_leave(uint32_t result,bool metadata_only) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); if (!state_.enabled) return;
    diagnostic_stage_=BStage::parser;
    if (metadata_only) {
        if (result) reject("native_metadata_parser_failed",{{"native_result",result}});
        else {
            catalog_hydrated_=true;
            owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"native_metadata_parser_succeeded",0,{{"native_result",result}});
        }
        return; // hydration doesn't prove a gameplay load or activate its map
    }
    state_.parser_result=result; state_.parser_completed=result==0;
    if (result) reject("native_load_parser_failed",{{"native_result",result},{"expected_result",0},{"source_verified",state_.source_verified}});
    else {
        state_.phase="parser_succeeded";
        owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"native_parser_succeeded",state_.operation,
            {{"native_result",result},{"source_verified",state_.source_verified}},load_data_);
    }
}
bool Campaign::menu_begin() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    diagnostic_stage_=BStage::transition;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"native_menu_begin",state_.operation,
        {{"accepting",owner_->accepts_requests()},{"initiated",initiated_},{"map_pending",map_pending_},
         {"continuity_persisted",state_.continuity_persisted},{"checkpoint",state_.checkpoint}});
    if (!owner_->accepts_requests()) return reject("lifecycle_after_session_refusal");
    if (!initiated_ || map_pending_) return reject("menu_before_campaign_transition_complete");
    // Keep the last gameplay checkpoint/map; a native menu is not a new AP map.
    // An already-owned SDK operation can finish, but no new save may start here.
    state_.map_active=false; state_.save_ready=false; menu_active_=true;
    if (!restart_record_.empty() && state_.checkpoint<=restart_checkpoint_) {
        state_.restart_pending=true; state_.restart_status=1;
    }
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"native_menu_admitted",state_.operation,
        {{"map_active",state_.map_active},{"save_ready",state_.save_ready},{"continuity_persisted",state_.continuity_persisted},{"checkpoint",state_.checkpoint}});
    return true;
}
bool Campaign::admit_generation(uint64_t generation,bool mission_launch,bool continuing) {
    if (mission_launch && generation!=state_.generation_after) return reject("lifecycle_mission_generation_mismatch",
        {{"generation",generation},{"expected_generation",state_.generation_after}});
    if (continuing) {
        if (generation!=state_.generation_after) return reject("lifecycle_continuation_generation_mismatch",
            {{"generation",generation},{"expected_generation",state_.generation_after}});
        if (state_.phase=="native_save_pending") return reject("lifecycle_continuation_save_pending");
    } else if (!state_.resumed && state_.phase!="native_start_queued") return reject("lifecycle_create_phase_mismatch");
    return true;
}
bool Campaign::admit_resume_map(const std::string& map,bool continuing,uint32_t stage_id,uint32_t native_subtype) {
    if (!continuing && state_.resumed) {
        if (!state_.parser_completed) return reject("lifecycle_resume_parser_incomplete");
        if (state_.loaded_difficulty!=options_.difficulty) return reject("lifecycle_resume_difficulty_mismatch");
        if (map!=(mission_destination_.empty()?state_.map:mission_destination_)) return reject("lifecycle_resume_map_mismatch");
        if (state_.stage_id && (stage_id!=state_.stage_id || native_subtype!=state_.native_subtype))
            return reject("lifecycle_resume_stage_mismatch");
    }
    return true;
}
bool Campaign::map_begin(std::string map,uint64_t generation,uint64_t event_id,uint32_t native_subtype,uint32_t stage_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); if (!state_.enabled) return true;
    menu_active_=false;
    diagnostic_stage_=BStage::transition;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"native_map_begin",state_.operation,
        {{"accepting",owner_->accepts_requests()},{"initiated",initiated_},{"map_pending",map_pending_},{"resumed",state_.resumed},
         {"map_bytes",map.size()},{"map_equal",map==state_.map},{"generation",generation},{"event_id",event_id},
         {"parser_completed",state_.parser_completed},{"loaded_difficulty",state_.loaded_difficulty},{"expected_difficulty",options_.difficulty}});
    if (!owner_->accepts_requests()) return reject("lifecycle_after_session_refusal");
    if (!initiated_) return reject("lifecycle_before_campaign_request");
    if (map_pending_) return reject("lifecycle_previous_transition_pending");
    if (map.empty() || map.size()>191) return reject("lifecycle_map_name_length");
    if (!stage_id) stage_id=default_stage(map,native_subtype);
    // Create/Continue constrain the first gameplay entry in this process.
    // Later native travel belongs to the completed active generation, not to
    // the original NewGame phase or the original Continue map/parser payload.
    const bool mission_launch=menu_save_source_==MenuSaveSource::active_fortress && !mission_destination_.empty();
    const bool continuing=state_.map_active && !mission_launch;
    if (!admit_generation(generation,mission_launch,continuing) ||
        !admit_resume_map(map,continuing,stage_id,native_subtype)) return false;
    if (map.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-.")!=std::string::npos)
        return reject("unsupported_native_map_name");
    if (continuing || !mission_destination_.empty()) {
        // keep the outgoing checkpoint on disk; its receipt can't confirm destination data before the destination saves
        state_.native_saved=false; state_.readback_verified=false; state_.continuity_persisted=false;
        state_.native_factory_matched=false;
    }
    prior_map_=state_.map;
    state_.map=std::move(map); state_.generation_before=generation; map_pending_=true;
    state_.native_subtype=native_subtype;
    state_.stage_id=stage_id;
    mission_destination_.clear();
    menu_save_source_=MenuSaveSource::none;
    state_.transition.event_id=event_id; state_.transition.generation_before=generation;
    state_.transition.generation_after=generation+1; state_.map_active=false; state_.save_ready=false;
    owner_->btrace.record(diagnostic_stage_,BStatus::succeeded,"native_map_admitted",state_.operation,
        {{"event_id",event_id},{"generation_before",generation},{"expected_generation",generation+1},{"resumed",state_.resumed}});
    return true;
}
void Campaign::map_end(const CampaignTransition& result) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); if (!state_.enabled || !map_pending_) return;
    const auto expected=state_.transition.event_id;
    diagnostic_stage_=BStage::transition;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"native_map_end",state_.operation,
        {{"event_id",result.event_id},{"expected_event",expected},{"generation_before",result.generation_before},
         {"generation_after",result.generation_after},{"expected_after",state_.generation_before+1},
         {"native_return",result.native_return},{"depth",result.depth},{"abnormal",result.abnormal},{"observed",result.observed},
         {"ended",result.ended},{"state_read",result.state_read},{"game",result.game},{"observation_reason",result.observation_reason}});
    state_.transition=result; state_.generation_after=result.generation_after; state_.effective_difficulty=result.difficulty;
    if (state_.reason!="none") return;
    const char* reason=nullptr;
    if (result.abnormal) reason="native_transition_abnormal";
    else if (!result.observed || !result.ended) reason="native_transition_not_observed";
    else if (!result.native_return) reason="native_transition_return_failed";
    else if (!result.state_read) reason="native_transition_state_unreadable";
    else if (result.observation_reason) reason="native_transition_observation_failed";
    else if (!expected || result.event_id!=expected || result.depth!=1) reason="native_transition_association_mismatch";
    else if (result.generation_before!=state_.generation_before || result.generation_after!=state_.generation_before+1) reason="native_transition_generation_mismatch";
    else if (result.game==SC_GAME_MAIN_MENU || result.game==SC_GAME_LOADING) {
        // ExecuteMapChange also has a successful early return while session
        // readiness is pending. Only this generation's native writer can later
        // confirm readiness; a diagnostic update or unrelated event cannot.
        state_.phase="native_transition_pending";
        owner_->btrace.record(diagnostic_stage_,BStatus::pending,"native_transition_readiness_pending",state_.operation,
            {{"event_id",result.event_id},{"generation_after",result.generation_after},{"game",result.game},{"native_return",result.native_return}}); return;
    }
    else if (result.game!=SC_GAME_IN_GAME) reason="native_transition_state_unsupported";
    else if (!result.map_read) reason="native_transition_map_unreadable";
    else if (state_.map!=result.map.data()) reason="native_transition_map_mismatch";
    else if (!result.difficulty_read) reason="native_transition_difficulty_unreadable";
    else if (result.difficulty!=options_.difficulty) reason="native_transition_difficulty_mismatch";
    if (reason) {
        if (result.game==SC_GAME_IN_GAME && result.state_read && result.native_return && !result.abnormal &&
            result.observed && result.ended && !result.observation_reason && result.event_id==expected && result.depth==1 &&
            result.generation_before==state_.generation_before && result.generation_after==state_.generation_before+1)
            reject(reason,{{"map_read",result.map_read},{"map_equal",state_.map==result.map.data()},
                {"difficulty_read",result.difficulty_read},{"actual_difficulty",result.difficulty},{"expected_difficulty",options_.difficulty}});
        else reject(reason);
        return;
    }
    complete_map();
}
void Campaign::complete_map() {
    if (!confirm_restart()) return;
    map_pending_=false; state_.map_active=true; state_.save_ready=true;
    if (state_.map=="game/hub/hub" && !prior_map_.empty() && prior_map_!="game/hub/hub")
        state_.end_summary_map=prior_map_;
    else if (state_.map!="game/hub/hub") state_.end_summary_map.clear();
    prior_map_.clear();
    if (!state_.operation) state_.phase=state_.resumed ? "reopened" : "native_created";
    owner_->btrace.record(BStage::transition,BStatus::succeeded,"native_map_ready",state_.operation,
        {{"resumed",state_.resumed},{"map_active",state_.map_active},{"save_ready",state_.save_ready},{"generation",state_.generation_after},
         {"effective_difficulty",state_.effective_difficulty},{"expected_difficulty",options_.difficulty},{"awaiting_checkpoint",checkpoint_awaiting_transition_.has_value()}});
    if (checkpoint_awaiting_transition_) {
        // the readback object may be gone; keep this operation's checked hashes, not the borrowed object
        engine::LocalMemory memory; persist_checkpoint(*checkpoint_awaiting_transition_,memory);
        checkpoint_awaiting_transition_.reset();
    }
}
bool Campaign::checkpoint_ready(const CampaignTransition& result) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    diagnostic_stage_=BStage::checkpoint_factory;
    state_.checkpoint_boundary=result;
    owner_->btrace.record(diagnostic_stage_,BStatus::entered,"native_checkpoint_boundary",state_.operation,
        {{"accepting",owner_->accepts_requests()},{"initiated",initiated_},{"observed",result.observed},{"observation_reason",result.observation_reason},
         {"depth",result.depth},{"generation",result.generation_after},{"expected_generation",state_.transition.generation_after},
         {"state_read",result.state_read},{"game",result.game},{"map_read",result.map_read},{"map_equal",state_.map==result.map.data()},
         {"difficulty_read",result.difficulty_read},{"actual_difficulty",result.difficulty},{"expected_difficulty",options_.difficulty}});
    if (!owner_->accepts_requests()) {
        owner_->btrace.record(diagnostic_stage_,BStatus::blocked,"checkpoint_after_terminal_session",state_.operation,
            {{"session_state",owner_->state()},{"session_fault",owner_->fault()}}); return false;
    }
    if (menu_save_pending_) {
        const bool fortress=menu_save_source_==MenuSaveSource::active_fortress;
        if ((!menu_active_ && !fortress) || !initiated_ || menu_save_source_==MenuSaveSource::none ||
            (menu_save_source_==MenuSaveSource::persisted_gameplay && !result.generation_after) ||
            !result.observed || result.observation_reason || result.depth ||
            result.generation_after!=menu_save_generation_ || !result.state_read ||
            result.game!=static_cast<uint32_t>(fortress ? SC_GAME_IN_GAME : SC_GAME_MAIN_MENU) ||
            (fortress && (!result.map_read || state_.map!=result.map.data() ||
                !result.difficulty_read || result.difficulty!=options_.difficulty)))
            return reject("native_mission_presave_boundary_mismatch");
        state_.save_ready=true;
        owner_->btrace.record(BStage::checkpoint_factory,BStatus::succeeded,"native_mission_presave_ready",state_.operation,
            {{"generation",result.generation_after},{"checkpoint",state_.checkpoint}});
        return true;
    }
    if (!initiated_ || !result.observed || result.observation_reason || result.depth>1 ||
        result.generation_after!=state_.transition.generation_after) return reject("native_checkpoint_transition_unassociated");
    if (!result.state_read || result.game!=SC_GAME_IN_GAME || !result.map_read || state_.map!=result.map.data())
        return reject("native_checkpoint_map_not_ready",{{"state_read",result.state_read},{"game",result.game},
            {"expected_game",SC_GAME_IN_GAME},{"map_read",result.map_read},{"map_equal",state_.map==result.map.data()}});
    if (!result.difficulty_read || result.difficulty!=options_.difficulty) return reject("native_checkpoint_difficulty_mismatch",
        {{"difficulty_read",result.difficulty_read},{"actual_difficulty",result.difficulty},{"expected_difficulty",options_.difficulty}});
    state_.save_ready=true;
    if (map_pending_ && state_.transition.ended) {
        // keep the original return's state/time. the separately recorded native writer confirms this pending generation's readiness
        state_.effective_difficulty=result.difficulty;
        complete_map();
    }
    const bool accepted=owner_->accepts_requests();
    if (accepted) owner_->btrace.record(BStage::checkpoint_factory,BStatus::succeeded,"native_checkpoint_ready",state_.operation,
        {{"generation",result.generation_after},{"expected_generation",state_.transition.generation_after},{"map_active",state_.map_active},
         {"save_ready",state_.save_ready},{"difficulty",result.difficulty},{"map_pending",map_pending_}});
    return accepted;
}
}
