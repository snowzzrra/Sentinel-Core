#pragma once
#include "sentinel_campaign_menu.h"
#include <array>
#include <mutex>

namespace sentinel::campaign_menu {
struct Projection {
    uint64_t revision=0;
    uint32_t count=0;
    uint32_t focus_id=0;
    std::array<sc_campaign_row,SC_CAMPAIGN_MENU_MAX_ROWS> rows{};
};
class Menu {
public:
    sc_campaign_result request(uint16_t operation,const sc_campaign_request&,bool admitted);
    Projection projection();
    void rendered(uint64_t revision);
    void selected(uint32_t id);
    uint32_t focus_id();
    void loaded(const char* map);
private:
    std::mutex mutex_;
    Projection staged_{},committed_{};
    std::array<bool,SC_CAMPAIGN_MENU_MAX_ROWS> received_{};
    uint64_t rendered_=0;
    uint32_t selected_=0,loaded_=0;
};
Menu& menu();
}
