// Copyright (c) 2026 snowzzrra. MIT; see ../LICENSE.
#include "save_campaign.h"
#include "save_session.h"
#include "save_catalog.h"
#include "save_provider.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
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
bool take(std::string_view& input, std::string_view key, std::string_view& out) {
    if (input.substr(0,key.size())!=key) return false;
    input.remove_prefix(key.size()); const auto end=input.find('\n');
    if (end==input.npos) return false; out=input.substr(0,end); input.remove_prefix(end+1); return true;
}
template<class T> bool number(std::string_view value,T& out) {
    if (value.empty() || (value.size()>1 && value[0]=='0')) return false;
    const auto r=std::from_chars(value.data(),value.data()+value.size(),out);
    return r.ec==std::errc{} && r.ptr==value.data()+value.size();
}
}
bool Campaign::configure(Session& owner,const storage::Descriptor& descriptor,storage::Namespace& lease) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (descriptor.campaign.intent==storage::CampaignIntent::none) return true;
    if (descriptor.campaign.difficulty>3 || owner_) return false;
    owner_=&owner; lease_=&lease; options_=descriptor.campaign; state_.enabled=true;
    state_.difficulty=options_.difficulty; state_.resumed=options_.intent==storage::CampaignIntent::resume;
    // Exact native suffix, never an index inferred from directory enumeration.
    state_.slot="AUTOSAVE0"; directory_=owner.native_root()+"/GAME-"+state_.slot;
    contract_="sentinel-campaign-v1\nnamespace="+owner.namespace_id()+"\ngeneration="+
        descriptor.identity.generation_fingerprint+"\nprovenance=synthetic-fixture\ncampaign=base\nstarting_stage=base_start\ndifficulty="+
        std::to_string(options_.difficulty)+"\nslot="+state_.slot+"\n";
    std::string record; auto r=lease.campaign_record(false,record);
    if (!state_.resumed) {
        if (r.ok() || r.win32_error!=ERROR_FILE_NOT_FOUND) return reject("create_requires_unreserved_namespace");
        r=lease.campaign_record(true,record);
        if (r.ok() || r.win32_error!=ERROR_FILE_NOT_FOUND) return reject("create_has_checkpoint_or_partial_metadata");
    } else {
        if (!r.ok() || record!=contract_) return reject("resume_contract_or_options_mismatch");
        contract_created_=true;
        if (!lease.campaign_record(true,record).ok() || !parse_checkpoint(record)) return reject("resume_checkpoint_incomplete_or_invalid");
        checkpoint_exists_=true;
        state_.source_checkpoint=state_.checkpoint;
    }
    state_.phase="armed"; return true;
}
bool Campaign::enabled() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return state_.enabled; }
CampaignSnapshot Campaign::snapshot() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return state_; }
bool Campaign::reject(const char* reason) {
    if (state_.reason=="none") state_.reason=reason;
    state_.phase="refused"; owner_->fail(SessionFault::native_campaign); return false;
}
void Campaign::refuse(const char* reason) { std::lock_guard<std::recursive_mutex> lock(mutex_); if (state_.enabled) reject(reason); }
bool Campaign::begin_create(bool clean,const std::string& slot,int32_t index,bool prospective) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    if (!owner_->accepts_requests() || initiated_ || state_.resumed || !clean || !prospective || index!=0 || slot!=state_.slot)
        return reject("fresh_process_and_owned_prospective_slot_required");
    if (!lease_->publish_campaign_record(false,contract_,true).ok()) return reject("create_contract_reservation_failed");
    contract_created_=true; initiated_=true; state_.phase="create_requested"; return true;
}
bool Campaign::begin_resume() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    if (!owner_->accepts_requests() || !state_.resumed || initiated_ || !checkpoint_exists_ || expected_.empty())
        return reject("explicit_resume_source_required");
    ProfileChoice choice{};
    if (!owner_->profile_choice(choice) || choice.name.data()!=state_.slot) return reject("resume_selection_mismatch");
    initiated_=true; state_.phase="resume_requested"; return true;
}
bool Campaign::start_internal(uint32_t difficulty,bool extra_life) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    if (!owner_->accepts_requests() || !initiated_ || state_.resumed || state_.phase!="create_requested" ||
        difficulty!=options_.difficulty || extra_life) return reject("native_creation_options_mismatch");
    state_.phase="native_start_queued"; return true;
}
bool Campaign::allow_difficulty(uint32_t value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || !initiated_) return true;
    if (state_.resumed && state_.parser_completed && !state_.map_active) state_.loaded_difficulty=value;
    if (value==options_.difficulty) return true;
    if (state_.resumed && state_.parser_completed && !state_.map_active) return reject("loaded_difficulty_contradicts_slot_options");
    if (state_.changes_blocked!=UINT32_MAX) ++state_.changes_blocked;
    return false; // Room managed: reject the change at the native setter, no frame spam.
}
bool Campaign::allow_access(uintptr_t data,const std::string& directory,bool write,bool erase) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || directory=="PROFILE") return true;
    // Native catalog hydration is read-only and precedes PROFILE admission.
    // It never associates a gameplay load; parser entry still requires the
    // explicit later LoadGameSlot operation and its exact SaveData identity.
    if (!write && !erase && state_.resumed && !initiated_ && state_.phase=="armed" &&
        owner_->native_io() && directory==directory_) return true;
    if (!owner_->accepts_requests() || !initiated_ || directory!=directory_ || erase)
        return reject("campaign_access_outside_authorized_slot");
    if (write) return state_.map_active || reject("campaign_write_before_native_map");
    if (!state_.resumed || state_.phase!="resume_requested" || (load_data_ && load_data_!=data))
        return reject("unassociated_campaign_load");
    load_data_=data; return true;
}
bool Campaign::save_record(const std::string& record,bool create) {
    if (!lease_->publish_campaign_record(true,record,create).ok()) return reject("checkpoint_metadata_publication_failed");
    checkpoint_exists_=true; return true;
}
bool Campaign::write_started(uint64_t operation,const std::string& directory,bool native_factory_matched) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || directory=="PROFILE") return true;
    if (!native_factory_matched || !operation || directory!=directory_ || !state_.map_active || state_.phase=="native_save_pending") return reject("save_operation_conflict");
    // Invalidate durable completion BEFORE this specific native write can mutate
    // its files. Interrupted/failed saves cannot reuse an older success receipt.
    if (!save_record(contract_+"state=native_save_pending\n",!checkpoint_exists_)) return false;
    state_.operation=operation; state_.native_factory_matched=true; state_.native_saved=false; state_.readback_verified=false;
    state_.continuity_persisted=false; state_.phase="native_save_pending"; return true;
}
std::string Campaign::checkpoint_text(const SdkWriteObservation& observation) const {
    std::string value=contract_+"checkpoint="+std::to_string(state_.checkpoint)+"\nmap="+state_.map+"\nfiles="+std::to_string(observation.payloads.size())+"\n";
    for (const auto& f:observation.payloads) value+="file="+std::string(f.name.data())+","+std::to_string(f.size)+","+hex(f.sha256)+"\n";
    return value+"state=native_saved_readback_verified\n";
}
bool Campaign::parse_checkpoint(std::string_view value) {
    if (value.substr(0,contract_.size())!=contract_) return false; value.remove_prefix(contract_.size());
    std::string_view part; uint32_t count=0;
    if (!take(value,"checkpoint=",part) || !number(part,state_.checkpoint) || !state_.checkpoint ||
        !take(value,"map=",part) || part.empty() || part.size()>191) return false;
    if (part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-.")!=part.npos) return false;
    state_.map=part;
    if (!take(value,"files=",part) || !number(part,count) || !count || count>64) return false;
    std::vector<SdkFileWrite> files;
    for (uint32_t i=0;i<count;++i) {
        if (!take(value,"file=",part)) return false;
        const auto comma=part.find(','), last=part.rfind(','); SdkFileWrite f{};
        if (comma==part.npos || comma==last || comma>=f.name.size() || part.substr(0,directory_.size()+1)!=directory_+"/" ||
            !number(part.substr(comma+1,last-comma-1),f.size) || !f.size || part.size()-last-1!=64) return false;
        std::memcpy(f.name.data(),part.data(),comma);
        if (std::any_of(files.begin(),files.end(),[&](const auto& p){return steam_name_equal(p.name.data(),f.name.data());})) return false;
        for (size_t n=0;n<32;++n) {
            const auto pair=part.substr(last+1+n*2,2); unsigned byte=0;
            const auto result=std::from_chars(pair.data(),pair.data()+2,byte,16);
            if (result.ec!=std::errc{} || result.ptr!=pair.data()+2) return false;
            f.sha256[n]=static_cast<unsigned char>(byte);
        }
        files.push_back(f);
    }
    if (value!="state=native_saved_readback_verified\n") return false;
    expected_=std::move(files); return true;
}
void Campaign::write_observed(uint64_t operation,bool terminal,bool successful,engine::Memory& memory) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || operation!=state_.operation) return;
    const auto actual=owner_->native_writes.snapshot(operation);
    state_.native_saved=actual.native_state==0 && actual.native_outcome==0 && actual.native_value==1 &&
        (actual.flags&SC_SAVE_WRITE_SDK_SUCCEEDED) && (actual.flags&SC_SAVE_WRITE_CALLBACKS_SUCCEEDED);
    state_.readback_verified=(actual.flags&SC_SAVE_WRITE_READBACK_HASHES) && (actual.flags&SC_SAVE_WRITE_READBACK_TERMINAL) && !(actual.flags&SC_SAVE_WRITE_READBACK_ERROR);
    if (!terminal) return;
    SdkWriteObservation manifest;
    if (!successful || !state_.native_saved || !state_.readback_verified || !owner_->native_writes.readback_manifest(operation,manifest) ||
        manifest.directory!=directory_ || manifest.payloads.size()>64) { reject("native_checkpoint_completion_unproven"); return; }
    ProfileChoice choice{}; ProfileWrite selection{};
    if (!owner_->profile_choice(choice) || !owner_->capture_profile_write(choice.name.data(),choice.index,0,selection) ||
        !owner_->persist_profile_write(selection,memory)) { reject("checkpoint_selection_persistence_failed"); return; }
    ++state_.checkpoint;
    if (!save_record(checkpoint_text(manifest),false)) return;
    expected_=manifest.payloads; state_.continuity_persisted=true; state_.phase="checkpoint_saved";
}
bool Campaign::verify_source(engine::Memory& memory,uintptr_t data,uintptr_t image) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || data!=load_data_) return true;
    uintptr_t files=0; int32_t count=0; std::string directory;
    bool valid=text(memory,data,0,directory) && directory==directory_ && read(memory,data,0x1c0,files) &&
        read(memory,data,0x1c8,count) && count>0 && static_cast<size_t>(count)==expected_.size();
    std::vector<std::string> seen;
    for (int32_t i=0;valid && i<count;++i) {
        uintptr_t file=0,table=0,buffer=0; uint64_t size=0,capacity=0; std::string name;
        valid=read(memory,files,static_cast<size_t>(i)*8,file) && read(memory,file,0,table) && table==image+0x2a575a8 && text(memory,file,8,name);
        const auto full=directory_+"/"+name;
        const auto expected=std::find_if(expected_.begin(),expected_.end(),[&](const auto& f){return steam_name_equal(f.name.data(),full);});
        std::array<unsigned char,32> hash{};
        valid=valid && expected!=expected_.end() && std::find(seen.begin(),seen.end(),full)==seen.end() &&
            read(memory,file,0x150,size) && size==expected->size && read(memory,file,0x158,capacity) && size<=capacity &&
            read(memory,file,0x168,buffer) && digest_payload(memory,buffer,expected->size,hash,GetTickCount64()+1000) && hash==expected->sha256;
        seen.push_back(full);
    }
    if (!valid) return reject("resume_payload_set_or_hash_mismatch");
    state_.source_verified=true; state_.phase="source_verified"; return true;
}
bool Campaign::parser_enter(uintptr_t data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled) return true;
    return (owner_->accepts_requests() && state_.resumed && state_.source_verified && load_data_==data) || reject("load_parser_source_not_correlated");
}
void Campaign::parser_leave(uint32_t result) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); if (!state_.enabled) return;
    state_.parser_result=result; state_.parser_completed=result==0;
    if (result) reject("native_load_parser_failed"); else state_.phase="parser_succeeded";
}
bool Campaign::map_begin(std::string map,uint64_t generation) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); if (!state_.enabled) return true;
    if (!owner_->accepts_requests() || !initiated_ || map_pending_ || map.empty() || map.size()>191 ||
        (!state_.resumed && state_.phase!="native_start_queued") ||
        (state_.resumed && (!state_.parser_completed || state_.loaded_difficulty!=options_.difficulty || map!=state_.map))) return reject("unexpected_campaign_lifecycle");
    if (map.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-.")!=std::string::npos)
        return reject("unsupported_native_map_name");
    state_.map=std::move(map); state_.generation_before=generation; map_pending_=true; state_.map_active=false; return true;
}
void Campaign::map_end(bool success,uint64_t generation,uint32_t difficulty) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); if (!state_.enabled || !map_pending_) return;
    map_pending_=false; state_.generation_after=generation; state_.effective_difficulty=difficulty;
    if (!success || generation<=state_.generation_before || difficulty!=options_.difficulty) { reject("native_map_or_difficulty_mismatch"); return; }
    state_.map_active=true; state_.phase=state_.resumed ? "reopened" : "native_created";
}
}
