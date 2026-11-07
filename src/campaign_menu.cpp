#include "campaign_menu.h"
#include "sentinel_inspection.h"
#include <cstring>
#include <algorithm>
#include <windows.h>

namespace sentinel::campaign_menu {
namespace {
bool same_owner(const sc_evidence_scope& a, const sc_evidence_scope& b) {
    return a.process.pid == b.process.pid && a.process.process_created == b.process.process_created &&
        !std::memcmp(a.process.instance_id, b.process.instance_id, 16) &&
        !std::memcmp(a.namespace_id, b.namespace_id, 65) && !std::memcmp(a.save_binding, b.save_binding, 16);
}
}
void Menu::entries_usable(const Projection& shown, const sc_evidence_scope& scope) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (scope.readiness != SC_EVIDENCE_READY || shown.revision != committed_.revision || shown.namespace_id != scope.namespace_id) return;
    for (uint32_t i = 0; i < shown.count; ++i) {
        const auto& row = shown.rows[i];
        constexpr auto usable = SC_CAMPAIGN_REVEALED | SC_CAMPAIGN_UNLOCKED;
        if ((row.flags & usable) != usable) continue;
        auto& out = access_[row.id];
        if (out.revision == shown.revision && same_owner(out.menu_scope, scope) &&
            out.menu_scope.process.lifecycle_generation == scope.process.lifecycle_generation) continue;
        if (pending_access_ == row.id) pending_access_ = 0;
        out = {}; out.menu_scope = scope; out.revision = shown.revision; out.stage_id = row.id;
        std::memcpy(out.map_name, row.map, sizeof(out.map_name)); out.entry_usable = 1;
    }
}
void Menu::load_requested(uint32_t id, const Projection& shown, const sc_evidence_scope& scope) {
    std::lock_guard<std::mutex> guard(mutex_);
    pending_access_ = 0;
    loaded_ = 0;
    const auto found = access_.find(id);
    if (found == access_.end() || scope.readiness != SC_EVIDENCE_READY) return;
    auto& out = found->second;
    if (out.revision != shown.revision || shown.revision != committed_.revision || !same_owner(out.menu_scope, scope) ||
        out.menu_scope.process.lifecycle_generation != scope.process.lifecycle_generation) return;
    out.destination_scope = {}; out.destination_revision = 0; out.destination_stage_id = 0; out.destination_ready = 0;
    pending_access_ = id;
}
void Menu::destination_ready(const sc_evidence_scope& scope) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!pending_access_ || scope.readiness != SC_EVIDENCE_READY) return;
    auto& out = access_.at(pending_access_);
    if (!same_owner(out.menu_scope, scope) || out.revision != committed_.revision) { pending_access_ = 0; return; }
    if (scope.process.lifecycle_generation <= out.menu_scope.process.lifecycle_generation) return;
    if (loaded_ == pending_access_ && !std::strcmp(scope.map_name, out.map_name)) {
        out.destination_scope = scope; out.destination_revision = out.revision;
        out.destination_stage_id = out.stage_id; out.destination_ready = 1;
    }
    pending_access_ = 0;
}
sc_mission_access_result Menu::access(uint32_t id, const sc_evidence_scope& scope) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto found = access_.find(id);
    if (scope.readiness != SC_EVIDENCE_READY || found == access_.end() || found->second.revision != committed_.revision ||
        !same_owner(found->second.menu_scope, scope)) return {};
    return found->second;
}
Menu& menu() { static Menu value; return value; }
static bool same_row(const Projection& p, const sc_campaign_request& r, const sc_campaign_summary& summary,
                     const sc_campaign_rewards* rewards) {
    const auto known = rewards ? rewards->known : 0, count = rewards ? rewards->count : 0;
    return !std::memcmp(&p.rows[r.index], &r.row, sizeof(r.row)) &&
           !std::memcmp(&p.summaries[r.index], &summary, sizeof(summary)) &&
           p.rewards_known[r.index] == known && p.rewards[r.index].size() == count &&
           (!count ||
            !std::memcmp(p.rewards[r.index].data(), rewards->entries, count * sizeof(sc_campaign_reward)));
}

static void validate_rows(const Projection& staged,
                          const std::array<bool, SC_CAMPAIGN_MENU_MAX_ROWS>& received,
                          sc_campaign_result& out) {
    const auto refuse = [&](uint32_t reason) {
        out.status = SC_CAMPAIGN_REFUSED;
        out.reason = reason;
    };
    unsigned hubs = 0;
    for (uint32_t i = 0; i < staged.count; ++i) {
        if (!received[i]) {
            refuse(SC_CAMPAIGN_INCOMPLETE);
            break;
        }
        const auto& row = staged.rows[i];
        hubs += (row.flags & SC_CAMPAIGN_HUB) != 0;
        for (uint32_t j = 0; j < i; ++j)
            if (row.id == staged.rows[j].id) refuse(SC_CAMPAIGN_ROWS);
    }
    if (!out.status && hubs != 1) refuse(SC_CAMPAIGN_ROWS);
}

void Menu::record_encounter_edge(uint32_t stage_id, const sc_campaign_reward& reward) {
    for (uint32_t j = 0; j < committed_.count; ++j)
        if (stage_id == committed_.rows[j].id)
            for (const auto& previous : committed_.rewards[j])
                if (previous.location_id == reward.location_id && previous.kind == reward.kind &&
                    !previous.checked)
                    encounter_edges_[stage_id] = {GetTickCount64(), reward};
}

void Menu::record_encounter_edges() {
    for (uint32_t i = 0; i < staged_.count; ++i)
        for (const auto& reward : staged_.rewards[i]) {
            if (reward.kind != SC_REWARD_ENCOUNTER || !reward.checked) continue;
            record_encounter_edge(staged_.rows[i].id, reward);
        }
}

void Menu::stage_request(uint16_t operation, const sc_campaign_request& r,
                         const sc_campaign_summary& presentation, const sc_campaign_rewards* rewards,
                         sc_campaign_result& out) {
    const auto refuse = [&](uint32_t reason) {
        out.status = SC_CAMPAIGN_REFUSED;
        out.reason = reason;
    };
    if (r.revision != staged_.revision) {
        staged_ = {};
        staged_.revision = r.revision;
        staged_.count = r.count;
        received_ = {};
        staged_.namespace_id = namespace_id_;
    }
    if (staged_.count != r.count)
        refuse(SC_CAMPAIGN_REVISION);
    else if (operation == campaign_row_operation) {
        if (received_[r.index] && !same_row(staged_, r, presentation, rewards))
            refuse(SC_CAMPAIGN_REVISION);
        else {
            staged_.rows[r.index] = r.row;
            staged_.summaries[r.index] = presentation;
            staged_.rewards_known[r.index] = rewards ? rewards->known : 0;
            if (rewards) staged_.rewards[r.index].assign(rewards->entries, rewards->entries + rewards->count);
            received_[r.index] = true;
        }
    } else {
        validate_rows(staged_, received_, out);
        if (!out.status) {
            record_encounter_edges();
            staged_.focus_id = r.row.id;
            committed_ = staged_;
            access_.clear();
            pending_access_ = 0;
        }
    }
}

void Menu::update_request(uint16_t operation, const sc_campaign_request& r,
                          const sc_campaign_summary& presentation, const sc_campaign_rewards* rewards,
                          sc_campaign_result& out) {
    const auto refuse = [&](uint32_t reason) {
        out.status = SC_CAMPAIGN_REFUSED;
        out.reason = reason;
    };
    if (!r.revision || r.revision < committed_.revision || r.revision < staged_.revision)
        refuse(SC_CAMPAIGN_REVISION);
    else if (!r.count || r.count > SC_CAMPAIGN_MENU_MAX_ROWS || r.index >= r.count)
        refuse(SC_CAMPAIGN_ROWS);
    else if (r.revision == committed_.revision) {
        if (r.count != committed_.count ||
            (operation == campaign_row_operation && !same_row(committed_, r, presentation, rewards)))
            refuse(SC_CAMPAIGN_REVISION);
    } else {
        stage_request(operation, r, presentation, rewards, out);
    }
}

sc_campaign_result Menu::request(uint16_t operation,const sc_campaign_request& r,bool admitted,
                                const sc_campaign_summary* summary, const sc_campaign_rewards* rewards) {
    std::lock_guard<std::mutex> guard(mutex_);
    sc_campaign_result out{}; out.size=sizeof(out); out.abi_version=SC_CAMPAIGN_MENU_ABI_VERSION;
    out.scope=r.execution.expected; out.request_id=r.execution.request_id;
    std::memcpy(out.nonce,r.execution.nonce,sizeof(out.nonce));
    std::memcpy(out.namespace_id,r.namespace_id,sizeof(out.namespace_id));
    const auto refuse=[&](uint32_t reason) { out.status=SC_CAMPAIGN_REFUSED; out.reason=reason; };
    if (admitted && namespace_id_!=r.namespace_id) {
        namespace_id_=r.namespace_id; staged_={}; committed_={}; received_={};
        rendered_=0; selected_=0; loaded_=0; hint_intents_.clear(); encounter_edges_.clear(); access_.clear(); pending_access_=0;
    }
    if (r.reward_presentation) out.abi_version=2;
    const sc_campaign_summary presentation=summary ? *summary : sc_campaign_summary{};
    if (!admitted)
        refuse(SC_CAMPAIGN_SCOPE);
    else if (operation!=campaign_inspect_operation) {
        update_request(operation, r, presentation, rewards, out);
    }
    // unauthenticated requests receive no existing menu state
    if (admitted) {
        out.committed_revision=committed_.revision; out.rendered_revision=rendered_;
        out.selected_id=selected_; out.loaded_id=loaded_;
        if (r.reward_presentation) {
            out.intent_count=static_cast<uint32_t>(hint_intents_.size());
            std::copy(hint_intents_.begin(),hint_intents_.end(),out.intent_ids);
        }
    }
    return out;
}
Projection Menu::projection() { std::lock_guard<std::mutex> guard(mutex_); return committed_; }
uint32_t Menu::loaded_id() { std::lock_guard<std::mutex> guard(mutex_); return loaded_; }
bool Menu::summary(uint32_t stage_id,const std::string& namespace_id,sc_campaign_summary& out,uint64_t* revision) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (revision) *revision=committed_.revision;
    if (committed_.namespace_id!=namespace_id) return false;
    for (uint32_t i=0;i<committed_.count;++i)
        if (stage_id==committed_.rows[i].id && (committed_.rows[i].flags&SC_CAMPAIGN_REVEALED)) {
            out=committed_.summaries[i]; return true;
        }
    return false;
}
bool Menu::encounter_reward(uint32_t stage_id,const std::string& namespace_id,uint64_t since,sc_campaign_reward& out) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto found=encounter_edges_.find(stage_id);
    if (committed_.namespace_id!=namespace_id) return false;
    if (found!=encounter_edges_.end() && found->second.first+2000>=since) { out=found->second.second; return true; }
    for (uint32_t i=0;i<committed_.count;++i)
        if (stage_id==committed_.rows[i].id)
            for (const auto& entry:committed_.rewards[i])
                if (entry.kind==SC_REWARD_ENCOUNTER) { out={}; return true; }
    return false;
}
void Menu::hint_intent(const char* namespace_id,uint32_t location_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (namespace_id_!=namespace_id || !location_id) return;
    for (uint32_t i=0;i<committed_.count;++i)
        for (const auto& reward:committed_.rewards[i]) if (reward.location_id==location_id) {
            if (hint_intents_.size()<96) hint_intents_.insert(location_id);
            return;
        }
}
void Menu::rendered(uint64_t revision) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (revision<=committed_.revision && revision>rendered_) rendered_=revision;
}
void Menu::selected(uint32_t id) {
    std::lock_guard<std::mutex> guard(mutex_); selected_=id; loaded_=0;
}
uint32_t Menu::focus_id() {
    std::lock_guard<std::mutex> guard(mutex_); return selected_ ? selected_ : committed_.focus_id;
}
void Menu::loaded(const char* map,uint32_t stage_id) {
    std::lock_guard<std::mutex> guard(mutex_); loaded_=0;
    if (stage_id) {
        for (uint32_t i=0;i<committed_.count;++i)
            if (committed_.rows[i].id==stage_id && !std::strcmp(committed_.rows[i].map,map)) {
                loaded_=stage_id; return;
            }
        return;
    }
    for (uint32_t i=0;i<committed_.count;++i)
        if (committed_.rows[i].id==selected_ && !std::strcmp(committed_.rows[i].map,map)) {
            loaded_=selected_; return;
        }
    for (uint32_t i=0;i<committed_.count;++i)
        if (!std::strcmp(committed_.rows[i].map,map)) { loaded_=committed_.rows[i].id; break; }
}
}
