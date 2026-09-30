#pragma once
#include "sentinel_campaign_menu.h"
#include <array>
#include <mutex>
#include <vector>
#include <set>
#include <string>

namespace sentinel::campaign_menu {
struct Projection {
    uint64_t revision=0;
    uint32_t count=0;
    uint32_t focus_id=0;
    std::string namespace_id;
    std::array<sc_campaign_row,SC_CAMPAIGN_MENU_MAX_ROWS> rows{};
    std::array<sc_campaign_summary,SC_CAMPAIGN_MENU_MAX_ROWS> summaries{};
    std::array<std::vector<sc_campaign_reward>,SC_CAMPAIGN_MENU_MAX_ROWS> rewards{};
    std::array<uint32_t,SC_CAMPAIGN_MENU_MAX_ROWS> rewards_known{};
};
class Menu {
public:
    sc_campaign_result request(uint16_t operation,const sc_campaign_request&,bool admitted,
                               const sc_campaign_summary* = nullptr, const sc_campaign_rewards* = nullptr);
    Projection projection();
    void rendered(uint64_t revision);
    void selected(uint32_t id);
    uint32_t focus_id();
    void loaded(const char* map);
    void hint_intent(const char* namespace_id,uint32_t location_id);
private:
    std::mutex mutex_;
    Projection staged_{},committed_{};
    std::array<bool,SC_CAMPAIGN_MENU_MAX_ROWS> received_{};
    uint64_t rendered_=0;
    uint32_t selected_=0,loaded_=0;
    std::string namespace_id_;
    std::set<uint32_t> hint_intents_;
};
Menu& menu();
}
