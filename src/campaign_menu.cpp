#include "campaign_menu.h"
#include "sentinel_inspection.h"
#include <cstring>

namespace sentinel::campaign_menu {
Menu& menu() { static Menu value; return value; }
sc_campaign_result Menu::request(uint16_t operation,const sc_campaign_request& r,bool admitted) {
    std::lock_guard<std::mutex> guard(mutex_);
    sc_campaign_result out{}; out.size=sizeof(out); out.abi_version=SC_CAMPAIGN_MENU_ABI_VERSION;
    out.scope=r.execution.expected; out.request_id=r.execution.request_id;
    std::memcpy(out.nonce,r.execution.nonce,sizeof(out.nonce));
    std::memcpy(out.namespace_id,r.namespace_id,sizeof(out.namespace_id));
    const auto refuse=[&](uint32_t reason) { out.status=SC_CAMPAIGN_REFUSED; out.reason=reason; };
    if (!admitted) refuse(SC_CAMPAIGN_SCOPE);
    else if (operation!=campaign_inspect_operation) {
        if (!r.revision || r.revision<committed_.revision || r.revision<staged_.revision)
            refuse(SC_CAMPAIGN_REVISION);
        else if (!r.count || r.count>SC_CAMPAIGN_MENU_MAX_ROWS || r.index>=r.count)
            refuse(SC_CAMPAIGN_ROWS);
        else if (r.revision==committed_.revision) {
            if (r.count!=committed_.count || (operation==campaign_row_operation &&
                std::memcmp(&committed_.rows[r.index],&r.row,sizeof(r.row)))) refuse(SC_CAMPAIGN_REVISION);
        } else {
            if (r.revision!=staged_.revision) {
                staged_={}; staged_.revision=r.revision; staged_.count=r.count; received_={};
            }
            if (staged_.count!=r.count) refuse(SC_CAMPAIGN_REVISION);
            else if (operation==campaign_row_operation) {
                if (received_[r.index] && std::memcmp(&staged_.rows[r.index],&r.row,sizeof(r.row)))
                    refuse(SC_CAMPAIGN_REVISION);
                else { staged_.rows[r.index]=r.row; received_[r.index]=true; }
            } else {
                unsigned hubs=0;
                for (uint32_t i=0;i<staged_.count;++i) {
                    if (!received_[i]) { refuse(SC_CAMPAIGN_INCOMPLETE); break; }
                    const auto& row=staged_.rows[i];
                    hubs+=(row.flags&SC_CAMPAIGN_HUB)!=0;
                    for (uint32_t j=0;j<i;++j) if (row.id==staged_.rows[j].id) refuse(SC_CAMPAIGN_ROWS);
                }
                if (!out.status && hubs!=1) refuse(SC_CAMPAIGN_ROWS);
                if (!out.status) { staged_.focus_id=r.row.id; committed_=staged_; }
            }
        }
    }
    // Unauthenticated requests receive no existing menu state.
    if (admitted) {
        out.committed_revision=committed_.revision; out.rendered_revision=rendered_;
        out.selected_id=selected_; out.loaded_id=loaded_;
    }
    return out;
}
Projection Menu::projection() { std::lock_guard<std::mutex> guard(mutex_); return committed_; }
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
void Menu::loaded(const char* map) {
    std::lock_guard<std::mutex> guard(mutex_); loaded_=0;
    for (uint32_t i=0;i<committed_.count;++i)
        if (!std::strcmp(committed_.rows[i].map,map)) { loaded_=committed_.rows[i].id; break; }
}
}
