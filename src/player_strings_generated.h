// GENERATED from DoomEternal-AP-Mod/data/player_strings.json; do not edit.
// regenerate in DoomEternal-AP-Mod: python -m tools.content.compile_player_strings
#pragma once
#include <string>

namespace sentinel::player_strings {
inline constexpr char controls_ammo_refill[] = "Ammo Refill";
inline constexpr char controls_binding_error[] = "%s - %s";
inline constexpr char controls_clear[] = "Clear";
inline constexpr char controls_game_key_conflict[] = "This key is assigned in the game.";
inline constexpr char controls_group[] = "AP Controls";
inline constexpr char controls_input_not_ready[] = "Game input is not ready.";
inline constexpr char controls_invalid_binding[] = "This AP binding is invalid.";
inline constexpr char controls_key_conflict[] = "This key is assigned to the other AP action.";
inline constexpr char controls_restore_failed[] = "The AP keys could not be restored.";
inline constexpr char controls_save_failed[] = "The AP key could not be saved.";
inline constexpr char controls_toggle_special_weapon[] = "Toggle Special Weapon";
inline constexpr char controls_unsupported_key[] = "Choose one supported key without modifiers.";
inline constexpr char hud_charges[] = "%u CHARGES";
inline constexpr char hud_crucible_selected[] = "CRUCIBLE SELECTED";
inline constexpr char hud_function_key_toggle[] = " [F%d TOGGLE]";
inline constexpr char hud_hammer_selected[] = "HAMMER SELECTED";
inline constexpr char hud_items_found[] = "ITEMS FOUND %u/%u";
inline constexpr char hud_key_toggle[] = " [%s TOGGLE]";
inline constexpr char hud_kills_progress[] = "KILLS %llu / %llu";
inline constexpr char hud_meter[] = "%.0f%% METER";
inline constexpr char hud_resource_unknown[] = "RESOURCE UNKNOWN";
inline std::string menu_challenge_completed(const char* challenge) { return std::string() + "CHALLENGE COMPLETED: " + challenge; }
inline constexpr char menu_choose_mission[] = "CHOOSE MISSION";
inline constexpr char menu_items_found[] = "ITEMS FOUND";
inline std::string menu_mastery_reward(const char* description, const char* newline, const char* reward) { return std::string() + description + newline + "REWARD: " + reward; }
inline constexpr char menu_mastery_unknown[] = "UNKNOWN";
inline constexpr char menu_recovery_changed[] = "Encounter changed; request cancelled";
inline constexpr char menu_recovery_finished[] = "Encounter finished";
inline constexpr char menu_recovery_no_active[] = "No active encounter";
inline constexpr char menu_recovery_pending[] = "Encounter recovery in progress";
inline constexpr char menu_recovery_unknown[] = "Recovery outcome unknown";
inline constexpr char menu_recovery_unsupported[] = "Unsupported encounter";
inline constexpr char menu_resume_checkpoint[] = "RESUME CHECKPOINT";
inline constexpr char menu_return_to_fortress[] = "RETURN TO FORTRESS";
inline constexpr char menu_reward_category[] = "ARCHIPELAGO";
inline constexpr char menu_reward_title[] = "ARCHIPELAGO REWARD";
}
