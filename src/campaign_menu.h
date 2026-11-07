#pragma once
#include "sentinel_campaign_menu.h"
#include "sentinel_evidence.h"
#include <array>
#include <mutex>
#include <vector>
#include <set>
#include <string>
#include <map>

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
    bool summary(uint32_t stage_id,const std::string& namespace_id,sc_campaign_summary& out,uint64_t* revision=nullptr);
    bool encounter_reward(uint32_t stage_id,const std::string& namespace_id,uint64_t since,sc_campaign_reward& out);
    void rendered(uint64_t revision);
    void selected(uint32_t id);
    uint32_t focus_id();
    uint32_t loaded_id();
    void loaded(const char* map,uint32_t stage_id=0);
    void entries_usable(const Projection&, const sc_evidence_scope&);
    void load_requested(uint32_t id, const Projection&, const sc_evidence_scope&);
    void destination_ready(const sc_evidence_scope&);
    sc_mission_access_result access(uint32_t id, const sc_evidence_scope&);
    void hint_intent(const char* namespace_id,uint32_t location_id);
private:
  void record_encounter_edge(uint32_t stage_id, const sc_campaign_reward&);
  void record_encounter_edges();
  void update_request(uint16_t operation, const sc_campaign_request&, const sc_campaign_summary&,
                      const sc_campaign_rewards*, sc_campaign_result&);
  void stage_request(uint16_t operation, const sc_campaign_request&, const sc_campaign_summary&,
                     const sc_campaign_rewards*, sc_campaign_result&);
  std::mutex mutex_;
  std::map<uint32_t, sc_mission_access_result> access_;
  uint32_t pending_access_ = 0;
  Projection staged_{}, committed_{};
  std::array<bool, SC_CAMPAIGN_MENU_MAX_ROWS> received_{};
  uint64_t rendered_ = 0;
  uint32_t selected_ = 0, loaded_ = 0;
  std::string namespace_id_;
  std::set<uint32_t> hint_intents_;
  std::map<uint32_t, std::pair<uint64_t, sc_campaign_reward>> encounter_edges_;
};
Menu& menu();
}
