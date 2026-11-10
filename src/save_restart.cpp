#include "save_campaign.h"
#include "save_session.h"
#include "campaign_stages_generated.h"
#include <charconv>
#include <windows.h>

namespace sentinel::save {
namespace {
bool field(std::string_view& text,std::string_view key,std::string_view& value) {
    const auto end=text.find('\n');
    if (text.substr(0,key.size())!=key || end==text.npos) return false;
    value=text.substr(key.size(),end-key.size()); text.remove_prefix(end+1); return true;
}
template<class T> bool number(std::string_view& text,std::string_view key,T& value) {
    std::string_view part;
    if (!field(text,key,part) || part.empty()) return false;
    const auto result=std::from_chars(part.data(),part.data()+part.size(),value);
    return result.ec==std::errc{} && result.ptr==part.data()+part.size() && std::to_string(value)==part;
}
bool restart_values(std::string_view text,uint64_t& sequence,uint32_t& stage,uint64_t& checkpoint,std::string& map) {
    std::string_view value;
    if (!number(text,"sequence=",sequence) || !sequence || !number(text,"stage=",stage) || !stage ||
        !number(text,"checkpoint=",checkpoint) || !field(text,"map=",value) || !text.empty()) return false;
    if (value.empty() || value.size()>191 || value.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-.")!=value.npos) return false;
    map=value; return true;
}
}
bool Campaign::read_restart() {
    const auto discard = [&](const char* reason) {
        owner_->btrace.record(BStage::resume,BStatus::succeeded,reason);
        if (!lease_->clear_restart_records().ok()) return reject("restart_record_clear_failed");
        state_.restart_pending=false; state_.restart_status=0; state_.restart_stage_id=0;
        state_.restart_map.clear(); restart_record_.clear();
        return true;
    };
    auto result=lease_->restart_record(false,restart_record_);
    if (result.win32_error==ERROR_FILE_NOT_FOUND) return true;
    state_.restart_pending=true; state_.restart_status=1;
    if (!result.ok()) return result.outcome==storage::Outcome::corrupt_manifest ?
        discard("restart_record_invalid") : reject("restart_record_unreadable");
    std::string_view text=restart_record_;
    const auto prefix=contract_+"stage-restart=1\n";
    if (text.substr(0,prefix.size())!=prefix) return discard("restart_record_identity_mismatch");
    text.remove_prefix(prefix.size());
    if (!restart_values(text,state_.restart_sequence,state_.restart_stage_id,restart_checkpoint_,state_.restart_map))
        return discard("restart_record_invalid");
    const auto route=campaign_menu::stage_route(state_.restart_stage_id);
    if (!route || state_.restart_map!=route->map)
        return discard("restart_stage_unavailable");
    std::string loaded;
    result=lease_->restart_record(true,loaded);
    if (!result.ok()) loaded.clear();
    // the receipt invalidates the outgoing checkpoint until a newer native save exists
    state_.restart_pending=loaded!=restart_record_ || state_.checkpoint<=restart_checkpoint_;
    state_.restart_status=state_.restart_pending?1u:2u;
    if (!state_.restart_pending) return discard("restart_record_complete");
    return true;
}
bool Campaign::final_death(uint32_t stage_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.enabled || !options_.restart_stage) return true;
    if (!owner_->accepts_requests() || !state_.map_active || state_.restart_pending) return false;
    const auto route=campaign_menu::stage_route(stage_id);
    if (!route || state_.map!=route->map) {
        owner_->btrace.record(BStage::deathlink,BStatus::succeeded,
            stage_id==1?"restart_hub_vanilla_reload":"restart_stage_unavailable_vanilla_reload");
        return true;
    }
    state_.restart_pending=true; state_.restart_status=1; state_.restart_stage_id=stage_id;
    state_.restart_map=state_.map; state_.restart_reason.clear(); ++state_.restart_sequence;
    restart_checkpoint_=state_.checkpoint;
    if (!persist_restart()) return false;
    owner_->btrace.record(BStage::deathlink,BStatus::pending,"restart_pending",state_.restart_sequence);
    return true;
}
bool Campaign::persist_restart() {
    restart_record_=contract_+"stage-restart=1\nsequence="+std::to_string(state_.restart_sequence)+
        "\nstage="+std::to_string(state_.restart_stage_id)+"\ncheckpoint="+std::to_string(restart_checkpoint_)+
        "\nmap="+state_.restart_map+"\n";
    if (lease_->publish_restart_record(false,restart_record_).ok()) return true;
    restart_failed("restart_record_write_failed"); return false;
}
bool Campaign::begin_restart() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!state_.restart_pending || !owner_->accepts_requests() || restart_loading_) return false;
    if (!checkpoint_exists_ || expected_.empty() || state_.phase=="native_save_pending" || map_pending_) {
        restart_failed("restart_checkpoint_not_ready"); return false;
    }
    restart_checkpoint_=state_.checkpoint;
    if (!persist_restart()) return false;
    restart_loading_=true;
    restart_deadline_=GetTickCount64()+120000;
    state_.map_active=false; state_.save_ready=false; menu_active_=true;
    state_.continuity_persisted=true;
    return true;
}
void Campaign::restart_tick(uint64_t now) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (restart_loading_ && state_.restart_status!=3 && now>=restart_deadline_)
        restart_failed("restart_load_timeout");
}
void Campaign::restart_failed(const char* reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    state_.restart_status=3; state_.restart_reason=reason;
    diagnostic_stage_=BStage::resume;
    reject(reason);
}
bool Campaign::confirm_restart() {
    if (!restart_loading_) return true;
    const auto route=campaign_menu::stage_route(state_.restart_stage_id);
    if (!route || state_.map!=state_.restart_map || state_.stage_id!=state_.restart_stage_id || state_.native_subtype!=route->variant) {
        restart_failed("restart_loaded_stage_mismatch"); return false;
    }
    if (!lease_->publish_restart_record(true,restart_record_).ok()) {
        restart_failed("restart_confirmation_write_failed"); return false;
    }
    restart_loading_=false; state_.restart_pending=false; state_.restart_status=2;
    owner_->btrace.record(BStage::transition,BStatus::succeeded,"restart_done",state_.restart_sequence);
    return true;
}
}
