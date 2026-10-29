// Generated from protocol/schema.json; regenerate: python -B tools/generate_protocol.py; schema-sha256: 6d1dd65bd4d2c3b57cc546d218544141143eb3797461a64e7b829556e0480a0c; generator-format: 1. DO NOT EDIT.
#pragma once
#include "sentinel_inspection.h"
#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
namespace sentinel::generated {
using Bytes = std::vector<uint8_t>;
inline constexpr size_t max_frame = 16384;
inline constexpr uint32_t wire_magic = 1346978643;
inline constexpr size_t header_size_1 = 16;
inline constexpr size_t header_size_2 = 32;
enum class Operation : uint16_t { read = 1, submit = 2, query = 3, cancel = 4, ack = 5, hello = 6 };
enum class WireError : uint16_t { ok = 0, incompatible_protocol = 1, capability_unavailable = 2, unsupported_operation = 3, malformed = 4 };
enum class CommandId : uint16_t { inspect = 1, engine = 2, context = 3, native = 4, diagnostic = 5, diagnostic_detail = 8, save = 11, save_admission = 12, save_write = 13, save_backup = 14, save_installation = 17, weapon_points = 18, campaign_row = 22, campaign_commit = 23, campaign = 24, inventory = 25, arsenal = 29, runes = 33, special = 37, deathlink = 41, automap = 45, command = 46, combat = 50, combat_presentation = 51, support_dump = 52 };
inline constexpr std::array<int64_t, 2> limit_execution_deadline_ms = {1, 5000};
inline constexpr std::array<int64_t, 2> limit_command_id_bytes = {1, 128};
inline constexpr uint64_t limit_namespace_bytes = 64ULL;
inline constexpr char limit_process_namespace[] = "0000000000000000000000000000000000000000000000000000000000000000";
inline constexpr std::array<int64_t, 2> limit_backup_campaign = {0, 2};
inline constexpr std::array<int64_t, 2> limit_backup_slot = {0, 11};
inline constexpr std::array<int64_t, 2> limit_backup_work_ms = {1, 60000};
inline constexpr std::array<int64_t, 2> limit_weapon_grant = {1, 117};
inline constexpr uint64_t limit_signed_balance_max = 2147483647ULL;
inline constexpr uint64_t limit_inventory_weapons_mask = 1023ULL;
inline constexpr uint64_t limit_inventory_equipment_mask = 31ULL;
inline constexpr std::array<int64_t, 2> limit_capacity_tier = {0, 4};
inline constexpr uint64_t limit_arsenal_mods_mask = 8191ULL;
inline constexpr uint64_t limit_arsenal_masteries_mask = 8191ULL;
inline constexpr uint64_t limit_arsenal_upgrade_mask = 268435455ULL;
inline constexpr uint64_t limit_runes_normal_mask = 511ULL;
inline constexpr uint64_t limit_runes_support_mask = 7ULL;
inline constexpr std::array<int64_t, 2> limit_rune_selection = {-1, 8};
inline constexpr uint64_t limit_special_balance_max = 1000ULL;
inline constexpr uint64_t limit_campaign_rows = 21ULL;
inline constexpr uint64_t limit_campaign_challenges = 3ULL;
inline constexpr uint64_t limit_campaign_rewards = 17ULL;
inline constexpr uint64_t limit_automap_bits = 576ULL;
inline constexpr uint64_t limit_automap_words = 9ULL;
inline constexpr uint64_t limit_combat_maps = 64ULL;
inline constexpr uint64_t limit_console_text_bytes = 2047ULL;
inline constexpr size_t execution_slots = 8;
inline constexpr size_t full_result_ms = 30000;
inline constexpr size_t identity_records = 65536;
struct v1_command_submit {
    uint64_t capability{};
    uint32_t expected_pid{};
    uint64_t expected_process_created{};
    std::array<uint8_t, 16> expected_instance_id{};
    uint64_t expected_generation{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t deadline_ms{};
    uint32_t command_abi{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    std::array<uint8_t, 2048> text{};
};
struct v1_command_result {
    uint64_t capability{};
    uint32_t identity_pid{};
    uint64_t identity_created{};
    std::array<uint8_t, 16> identity_instance{};
    uint32_t core_abi{};
    std::string product_version{};
    std::string build_id{};
    uint32_t domain_abi{};
    uint32_t execution_state{};
    uint32_t execution_reason{};
    uint32_t execution_cancel_requested{};
    uint32_t execution_retrieved{};
    uint32_t execution_scope_pid{};
    uint64_t execution_scope_process_created{};
    std::array<uint8_t, 16> execution_scope_instance_id{};
    uint64_t execution_scope_generation{};
    uint64_t execution_request_id{};
    std::array<uint8_t, 16> execution_nonce{};
    uint64_t execution_admitted_ms{};
    uint64_t execution_deadline_ms{};
    uint64_t execution_claimed_ms{};
    uint64_t execution_observed_ms{};
    uint64_t execution_executed_ms{};
    uint64_t execution_completed_ms{};
    uint64_t execution_retrieved_ms{};
    uint32_t execution_thread{};
    uint32_t execution_site_revision{};
    uint32_t execution_phase{};
    uint32_t execution_lifecycle{};
    uint32_t execution_game_state{};
    uint32_t execution_map_validity{};
    uint32_t execution_map_reason{};
    uint32_t execution_map_error{};
    uint32_t execution_map_length{};
    std::string execution_map{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t native_exception{};
};
struct v1_inventory_result {
    uint64_t capability{};
    uint32_t identity_pid{};
    uint64_t identity_created{};
    std::array<uint8_t, 16> identity_instance{};
    uint32_t core_abi{};
    std::string product_version{};
    std::string build_id{};
    uint32_t domain_abi{};
    uint32_t execution_state{};
    uint32_t execution_reason{};
    uint32_t execution_cancel_requested{};
    uint32_t execution_retrieved{};
    uint32_t execution_scope_pid{};
    uint64_t execution_scope_process_created{};
    std::array<uint8_t, 16> execution_scope_instance_id{};
    uint64_t execution_scope_generation{};
    uint64_t execution_request_id{};
    std::array<uint8_t, 16> execution_nonce{};
    uint64_t execution_admitted_ms{};
    uint64_t execution_deadline_ms{};
    uint64_t execution_claimed_ms{};
    uint64_t execution_observed_ms{};
    uint64_t execution_executed_ms{};
    uint64_t execution_completed_ms{};
    uint64_t execution_retrieved_ms{};
    uint32_t execution_thread{};
    uint32_t execution_site_revision{};
    uint32_t execution_phase{};
    uint32_t execution_lifecycle{};
    uint32_t execution_game_state{};
    uint32_t execution_map_validity{};
    uint32_t execution_map_reason{};
    uint32_t execution_map_error{};
    uint32_t execution_map_length{};
    std::string execution_map{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t flags{};
    uint32_t native_exception{};
    uint32_t weapons_before{};
    uint32_t weapons_after{};
    uint32_t equipment_before{};
    uint32_t equipment_after{};
    uint32_t special_before{};
    uint32_t special_after{};
    uint32_t upgrades_before{};
    uint32_t upgrades_after{};
    uint8_t health_before{};
    uint8_t health_after{};
    uint8_t armor_before{};
    uint8_t armor_after{};
    uint8_t ammo_before{};
    uint8_t ammo_after{};
    uint8_t reserved_before{};
    uint8_t reserved_after{};
    uint64_t operations_applied{};
    uint8_t ice_before{};
    uint8_t ice_after{};
    uint8_t dash_before{};
    uint8_t dash_after{};
    uint8_t key_before{};
    uint8_t key_after{};
    uint8_t frag_before{};
    uint8_t frag_after{};
    uint8_t flame_before{};
    uint8_t flame_after{};
    uint8_t punch_before{};
    uint8_t punch_after{};
    uint64_t perks_known{};
    uint64_t perks_owned{};
};
struct v1_inspect_request {
    uint64_t capability{};
};
struct v1_diagnostic_detail_submit {
    uint64_t capability{};
    uint32_t expected_pid{};
    uint64_t expected_process_created{};
    std::array<uint8_t, 16> expected_instance_id{};
    uint64_t expected_generation{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t deadline_ms{};
};
struct campaign_reward {
    uint32_t location_id{};
    uint32_t kind{};
    uint32_t checked{};
    std::array<uint8_t, 80> unlockable{};
    std::array<uint8_t, 128> name{};
    std::array<uint8_t, 512> text{};
};
struct v1_campaign_rewards_row {
    uint64_t capability{};
    uint32_t expected_pid{};
    uint64_t expected_process_created{};
    std::array<uint8_t, 16> expected_instance_id{};
    uint64_t expected_generation{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t deadline_ms{};
    std::array<uint8_t, 65> namespace_id{};
    uint64_t revision{};
    uint32_t index{};
    uint32_t count{};
    uint32_t row_id{};
    uint32_t row_flags{};
    uint32_t native_index{};
    std::array<uint8_t, 192> map{};
    std::array<uint8_t, 96> title{};
    uint32_t known{};
    uint32_t found{};
    uint32_t total{};
    uint32_t band{};
    std::array<uint8_t, 80> challenge_0_unlockable{};
    uint32_t challenge_0_found{};
    uint32_t challenge_0_required{};
    uint32_t challenge_0_checked{};
    std::array<uint8_t, 80> challenge_1_unlockable{};
    uint32_t challenge_1_found{};
    uint32_t challenge_1_required{};
    uint32_t challenge_1_checked{};
    std::array<uint8_t, 80> challenge_2_unlockable{};
    uint32_t challenge_2_found{};
    uint32_t challenge_2_required{};
    uint32_t challenge_2_checked{};
    uint32_t rewards_known{};
    std::vector<campaign_reward> rewards{};
};
struct v1_automap_publish {
    uint64_t capability{};
    uint32_t expected_pid{};
    uint64_t expected_process_created{};
    std::array<uint8_t, 16> expected_instance_id{};
    uint64_t expected_generation{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t deadline_ms{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t known{};
    uint64_t revision{};
    uint64_t checked_0{};
    uint64_t checked_1{};
    uint64_t checked_2{};
    uint64_t checked_3{};
    uint64_t checked_4{};
    uint64_t checked_5{};
    uint64_t checked_6{};
    uint64_t checked_7{};
    uint64_t checked_8{};
};
struct v1_campaign_base {
    uint64_t capability{};
    uint32_t expected_pid{};
    uint64_t expected_process_created{};
    std::array<uint8_t, 16> expected_instance_id{};
    uint64_t expected_generation{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t deadline_ms{};
    std::array<uint8_t, 65> namespace_id{};
    uint64_t revision{};
    uint32_t index{};
    uint32_t count{};
    uint32_t row_id{};
    uint32_t row_flags{};
    uint32_t native_index{};
    std::array<uint8_t, 192> map{};
    std::array<uint8_t, 96> title{};
};
struct v1_campaign_presentation {
    uint64_t capability{};
    uint32_t expected_pid{};
    uint64_t expected_process_created{};
    std::array<uint8_t, 16> expected_instance_id{};
    uint64_t expected_generation{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t deadline_ms{};
    std::array<uint8_t, 65> namespace_id{};
    uint64_t revision{};
    uint32_t index{};
    uint32_t count{};
    uint32_t row_id{};
    uint32_t row_flags{};
    uint32_t native_index{};
    std::array<uint8_t, 192> map{};
    std::array<uint8_t, 96> title{};
    uint32_t known{};
    uint32_t found{};
    uint32_t total{};
    uint32_t band{};
    std::array<uint8_t, 80> challenge_0_unlockable{};
    uint32_t challenge_0_found{};
    uint32_t challenge_0_required{};
    uint32_t challenge_0_checked{};
    std::array<uint8_t, 80> challenge_1_unlockable{};
    uint32_t challenge_1_found{};
    uint32_t challenge_1_required{};
    uint32_t challenge_1_checked{};
    std::array<uint8_t, 80> challenge_2_unlockable{};
    uint32_t challenge_2_found{};
    uint32_t challenge_2_required{};
    uint32_t challenge_2_checked{};
};
struct v1_runes_request {
    uint64_t capability{};
    uint32_t expected_pid{};
    uint64_t expected_process_created{};
    std::array<uint8_t, 16> expected_instance_id{};
    uint64_t expected_generation{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t deadline_ms{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t normal_runes{};
    uint32_t support_runes{};
    uint8_t select_slot{};
    int8_t select_rune{};
    std::array<uint8_t, 2> reserved{};
};
struct scope {
    uint32_t pid{};
    uint64_t process_created{};
    std::array<uint8_t, 16> instance_id{};
    uint64_t generation{};
};
struct command_reference {
    scope scope{};
    std::array<uint8_t, 64> namespace_id{};
    std::string command_id{};
    uint16_t command_type{};
    uint16_t payload_revision{};
    std::array<uint8_t, 32> fingerprint{};
};
struct submission_identity {
    scope scope{};
    std::array<uint8_t, 64> namespace_id{};
    std::string command_id{};
    uint16_t command_type{};
    uint16_t payload_revision{};
};
struct submission {
    submission_identity reference{};
    std::array<uint8_t, 16> save_binding{};
    uint32_t deadline_ms{};
};
struct ack {
    command_reference reference{};
    std::array<uint8_t, 16> result_token{};
};
struct result_dimensions {
    uint32_t progress{};
    uint32_t execution{};
    uint32_t effect{};
    uint32_t retention{};
    std::array<uint8_t, 16> result_token{};
};
struct sc_status {
    uint32_t abi_version{};
    uint64_t capabilities{};
    uint32_t state{};
    uint32_t last_result{};
    uint32_t initialization_count{};
    uint32_t diagnostic_count{};
    std::array<uint8_t, 32> version{};
    std::array<uint8_t, 65> build_id{};
};
struct sc_engine_field {
    uint32_t validity{};
    uint32_t reason{};
    int32_t value{};
    uint32_t win32_error{};
};
struct sc_engine_snapshot {
    uint32_t abi_version{};
    uint64_t sequence{};
    uint64_t sampled_at_ms{};
    uint32_t duration_ms{};
    uint32_t sample_reason{};
    uint32_t pe_reason{};
    uint32_t machine{};
    uint32_t timestamp{};
    uint32_t image_size{};
    uint32_t entry_rva{};
    uint32_t disk_hash_reason{};
    std::array<uint8_t, 65> disk_sha256{};
    uint32_t profile{};
    uint32_t locator_revision{};
    uint32_t root_locator_reason{};
    uint32_t root_signature_rva{};
    uint32_t root_target_rva{};
    uint32_t profile_runtime_validated{};
    std::array<sc_engine_field, 6> fields{};
};
struct sc_context_map {
    uint32_t validity{};
    uint32_t reason{};
    uint32_t win32_error{};
    uint32_t length{};
    std::array<uint8_t, 256> bytes{};
};
struct sc_context_field {
    uint32_t validity{};
    uint32_t reason{};
    uint64_t value{};
    uint32_t win32_error{};
};
struct sc_context_snapshot {
    uint32_t abi_version{};
    uint32_t pid{};
    uint64_t process_created{};
    std::array<uint8_t, 16> instance{};
    uint64_t sequence{};
    uint64_t sampled_at_ms{};
    uint32_t duration_ms{};
    uint32_t sample_reason{};
    uint32_t profile{};
    uint32_t locator_revision{};
    uint32_t layout_revision{};
    uint32_t root_locator_reason{};
    uint32_t disk_hash_reason{};
    std::array<uint8_t, 65> disk_sha256{};
    sc_context_map current_map{};
    std::array<sc_context_field, 5> fields{};
};
struct sc_native_scope {
    uint32_t pid{};
    uint64_t process_created{};
    std::array<uint8_t, 16> instance_id{};
    uint64_t lifecycle_generation{};
};
struct sc_native_event {
    uint64_t sequence{};
    uint64_t generation{};
    uint64_t at_ms{};
    uint32_t kind{};
    uint32_t lifecycle{};
    uint32_t thread_id{};
    uint32_t depth{};
};
struct sc_native_snapshot {
    uint32_t abi_version{};
    sc_native_scope scope{};
    uint32_t availability{};
    uint32_t reason{};
    uint32_t site_revision{};
    uint32_t site_rva{};
    uint32_t phase{};
    uint32_t installed_hooks{};
    std::array<uint32_t, 3> validator_reasons{};
    uint32_t retained_module{};
    uint32_t coverage{};
    uint32_t lifecycle{};
    uint32_t depth{};
    uint32_t checkpoint_flag_known{};
    uint32_t checkpoint_flag{};
    uint64_t event_sequence{};
    uint64_t event_gap_count{};
    uint64_t history_oldest{};
    uint64_t history_overwritten{};
    uint64_t callback_sequence{};
    uint64_t callback_at_ms{};
    uint32_t callback_thread_id{};
    uint32_t native_owner_thread_id{};
    uint64_t context_generation{};
    uint64_t context_sampled_at_ms{};
    uint32_t context_reason{};
    uint32_t game_state{};
    sc_context_map current_map{};
    uint32_t queued{};
    uint32_t claimed{};
    uint32_t retained_results{};
    uint32_t history_gap{};
    uint32_t event_count{};
    std::array<sc_native_event, 6> events{};
};
struct sc_diagnostic_result {
    uint32_t state{};
    uint32_t reason{};
    uint32_t cancel_requested{};
    uint32_t retrieved{};
    sc_native_scope scope{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint64_t admitted_at_ms{};
    uint64_t deadline_at_ms{};
    uint64_t claimed_at_ms{};
    uint64_t observed_at_ms{};
    uint64_t executed_at_ms{};
    uint64_t completed_at_ms{};
    uint64_t retrieved_at_ms{};
    uint32_t thread_id{};
    uint32_t site_revision{};
    uint32_t phase{};
    uint32_t lifecycle{};
    uint32_t game_state{};
    sc_context_map current_map{};
};
struct sc_save_field {
    uint32_t validity{};
    uint32_t reason{};
    uint64_t value{};
    uint32_t win32_error{};
    uint32_t reserved{};
};
struct sc_save_snapshot {
    uint32_t abi_version{};
    uint32_t pid{};
    uint32_t profile{};
    uint64_t process_created{};
    std::array<uint8_t, 16> instance{};
    uint64_t sequence{};
    uint64_t sampled_at_ms{};
    uint32_t duration_ms{};
    uint32_t sample_reason{};
    uint32_t layout_revision{};
    uint32_t root_locator_reason{};
    uint32_t mutation_available{};
    uint32_t mutation_reason{};
    std::array<sc_save_field, 12> fields{};
};
struct sc_save_admission_snapshot {
    uint32_t abi_version{};
    uint32_t state{};
    uint32_t fault{};
    uint32_t prepared_routes{};
    uint32_t required_routes{};
    uint32_t flags{};
    uint32_t reserved{};
    std::array<uint8_t, 65> namespace_id{};
    std::array<uint8_t, 44> native_root{};
    std::array<uint8_t, 19> reserved_bytes{};
};
struct sc_save_write_snapshot {
    uint32_t abi_version{};
    uint32_t state{};
    uint32_t flags{};
    uint64_t operation_id{};
    uint64_t sdk_sequence{};
    uint32_t file_count{};
    uint32_t submitted{};
    uint32_t completed{};
    uint32_t pending_handles{};
    uint32_t preparation_jobs{};
    uint32_t preflight_jobs{};
    uint32_t native_value{};
    uint32_t reserved{};
    int64_t native_state{};
    int64_t native_outcome{};
    std::array<uint8_t, 64> directory{};
    std::array<uint8_t, 16> reserved_bytes{};
};
struct sc_save_backup_snapshot {
    uint32_t abi_version{};
    uint32_t state{};
    uint32_t failure{};
    sc_diagnostic_result execution{};
    uint64_t operation_id{};
    uint32_t flags{};
    uint32_t native_exception{};
    uint32_t storage_outcome{};
    uint32_t storage_error{};
    uint32_t files{};
    uint64_t bytes{};
    std::array<uint8_t, 80> basename{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t campaign{};
    uint32_t slot{};
};
struct sc_install_event {
    uint64_t sequence{};
    uint64_t at_ms{};
    uint64_t duration_ms{};
    uint32_t stage{};
    uint32_t target_group{};
    uint32_t target_index{};
    uint32_t rva{};
    uint32_t signature_offset{};
    uint32_t result{};
    uint32_t reason{};
    uint32_t read_reason{};
    uint32_t win32_error{};
    uint32_t minhook_status{};
    uint32_t byte_count{};
    uint32_t collision_rva{};
    uint32_t byte_window_offset{};
    std::array<uint8_t, 32> expected_bytes{};
    std::array<uint8_t, 32> actual_bytes{};
};
struct sc_save_installation_snapshot {
    uint32_t abi_version{};
    uint32_t attempt{};
    uint32_t phase{};
    uint32_t last_completed_stage{};
    uint32_t startup_observation{};
    uint32_t validated{};
    uint32_t created{};
    uint32_t enabled{};
    uint32_t cleanup_failures{};
    uint32_t gaps{};
    uint32_t reserved{};
    uint64_t sequence{};
    sc_install_event active{};
    sc_install_event primary_failure{};
    sc_install_event cleanup_failure{};
};
struct sc_weapon_points_result {
    uint32_t abi_version{};
    sc_diagnostic_result execution{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t amount{};
    uint32_t expected_gained{};
    uint32_t outcome{};
    uint32_t flags{};
    uint32_t native_exception{};
    uint32_t balance_before{};
    uint32_t balance_after{};
    uint32_t gained_before{};
    uint32_t gained_after{};
    uint64_t suppressed_grants{};
};
struct sc_campaign_result {
    uint32_t abi_version{};
    sc_native_scope scope{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t status{};
    uint32_t reason{};
    uint64_t committed_revision{};
    uint64_t rendered_revision{};
    uint32_t selected_id{};
    uint32_t loaded_id{};
    uint32_t intent_count{};
    std::array<uint32_t, 96> intent_ids{};
};
struct sc_inventory_result {
    uint32_t abi_version{};
    sc_diagnostic_result execution{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t flags{};
    uint32_t native_exception{};
    uint32_t weapons_before{};
    uint32_t weapons_after{};
    uint32_t equipment_before{};
    uint32_t equipment_after{};
    uint32_t special_before{};
    uint32_t special_after{};
    uint32_t upgrades_before{};
    uint32_t upgrades_after{};
    uint8_t health_tier_before{};
    uint8_t health_tier_after{};
    uint8_t armor_tier_before{};
    uint8_t armor_tier_after{};
    uint8_t ammo_tier_before{};
    uint8_t ammo_tier_after{};
    uint8_t reserved_before{};
    uint8_t reserved_after{};
    uint64_t operations_applied{};
    uint8_t ice_bomb_before{};
    uint8_t ice_bomb_after{};
    uint8_t dash_before{};
    uint8_t dash_after{};
    uint8_t slayer_key_before{};
    uint8_t slayer_key_after{};
    uint8_t frag_grenade_before{};
    uint8_t frag_grenade_after{};
    uint8_t flame_belch_before{};
    uint8_t flame_belch_after{};
    uint8_t blood_punch_before{};
    uint8_t blood_punch_after{};
    uint64_t perks_known{};
    uint64_t perks_owned{};
};
struct sc_arsenal_result {
    uint32_t abi_version{};
    sc_diagnostic_result execution{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t flags{};
    uint32_t native_exception{};
    uint32_t weapons_before{};
    uint32_t weapons_after{};
    uint32_t mods_before{};
    uint32_t mods_after{};
    std::array<uint8_t, 8> selected_mods_before{};
    std::array<uint8_t, 8> selected_mods_after{};
    uint32_t normal_upgrades_before{};
    uint32_t normal_upgrades_after{};
    uint16_t masteries_ap_before{};
    uint16_t masteries_ap_after{};
    uint16_t mastery_challenges_active_before{};
    uint16_t mastery_challenges_active_after{};
    uint16_t mastery_challenges_completed_before{};
    uint16_t mastery_challenges_completed_after{};
    uint16_t masteries_effective_before{};
    uint16_t masteries_effective_after{};
    uint32_t mission_challenges_active_before{};
    uint32_t mission_challenges_active_after{};
    uint32_t mission_challenges_completed_before{};
    uint32_t mission_challenges_completed_after{};
    uint64_t operations_applied{};
};
struct sc_runes_result {
    uint32_t abi_version{};
    sc_diagnostic_result execution{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t flags{};
    uint32_t native_exception{};
    uint32_t owned_normal_before{};
    uint32_t owned_normal_after{};
    uint32_t owned_support_before{};
    uint32_t owned_support_after{};
    std::array<int8_t, 3> selected_slots_before{};
    std::array<int8_t, 3> selected_slots_after{};
    int8_t selected_support_before{};
    int8_t selected_support_after{};
    uint8_t unlocked_slots_before{};
    uint8_t unlocked_slots_after{};
    uint8_t derived_pairs_before{};
    uint8_t derived_pairs_after{};
    uint8_t reserved_before{};
    uint8_t reserved_after{};
    uint64_t operations_applied{};
};
struct sc_special_result {
    uint32_t abi_version{};
    sc_diagnostic_result execution{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t flags{};
    uint32_t native_exception{};
    uint32_t owns_crucible{};
    uint32_t owns_hammer{};
    uint32_t hammer_tier{};
    uint32_t selected{};
    uint32_t native_crucible{};
    uint32_t native_hammer{};
    uint32_t native_hammer_perks{};
    uint32_t native_selected{};
    uint32_t native_state_known{};
    uint32_t crucible_charge{};
    uint32_t crucible_charge_max{};
    uint32_t refill_balance{};
    uint32_t refill_flags{};
    uint64_t refill_request_id{};
    uint32_t refill_request_state{};
    uint32_t refill_executed{};
    uint64_t refill_execution_id{};
    uint32_t refill_sequence{};
    uint32_t reserved0{};
    uint64_t operations_applied{};
};
struct sc_deathlink_result {
    uint32_t abi_version{};
    sc_diagnostic_result execution{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t flags{};
    uint32_t native_exception{};
    uint32_t enabled{};
    uint32_t mode{};
    uint32_t remote_state{};
    uint32_t remote_protection{};
    uint32_t remote_attempts{};
    uint32_t pending_count{};
    uint64_t remote_event_id{};
    uint32_t local_candidates{};
    uint32_t local_suppressed{};
    uint32_t local_state{};
    uint64_t local_death_sequence{};
    uint64_t local_death_time_ms{};
    uint32_t local_cause{};
    uint32_t local_protection{};
    uint32_t local_flags{};
    uint32_t reserved0{};
    uint64_t operations_applied{};
};
struct sc_automap_result {
    uint32_t abi_version{};
    sc_native_scope scope{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t known{};
    uint32_t native_fault{};
    uint64_t revision{};
    uint64_t scanned{};
    uint64_t collected{};
    uint64_t completed_passes{};
};
struct sc_command_result {
    uint32_t abi_version{};
    sc_diagnostic_result execution{};
    std::array<uint8_t, 65> namespace_id{};
    uint32_t kind{};
    uint32_t outcome{};
    uint32_t native_exception{};
};
struct sc_combat_snapshot {
    uint32_t abi_version{};
    sc_native_scope scope{};
    uint32_t availability{};
    uint32_t reason{};
    uint64_t observation_gaps{};
    std::array<uint8_t, 65> namespace_id{};
    uint64_t qualified_kills{};
};
struct sc_combat_presentation_result {
    uint32_t abi_version{};
    sc_native_scope scope{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t status{};
    uint32_t reason{};
    uint64_t total{};
    uint64_t target{};
    uint64_t milestone{};
    uint32_t visible{};
};
struct inspect_inspect_payload {
};
struct engine_engine_payload {
};
struct context_context_payload {
};
struct native_native_payload {
    uint64_t after_event{};
};
struct diagnostic_diagnostic_payload {
};
struct diagnostic_detail_diagnostic_detail_payload {
};
struct save_save_payload {
};
struct save_admission_save_admission_payload {
};
struct save_write_save_write_payload {
    uint64_t operation_id{};
};
struct save_backup_save_backup_payload {
    uint32_t campaign{};
    uint32_t slot{};
    uint32_t work_deadline_ms{};
};
struct save_installation_save_installation_payload {
};
struct weapon_points_observe_payload {
};
struct weapon_points_grant_payload {
    uint32_t amount{};
    uint32_t expected_gained{};
};
struct sc_campaign_row {
    uint32_t id{};
    uint32_t flags{};
    uint32_t native_index{};
    std::array<uint8_t, 192> map{};
    std::array<uint8_t, 96> title{};
};
struct sc_physical_challenge {
    std::array<uint8_t, 80> unlockable{};
    uint32_t found{};
    uint32_t required{};
    uint32_t checked{};
};
struct sc_campaign_summary {
    uint32_t known{};
    uint32_t found{};
    uint32_t total{};
    uint32_t band{};
    std::array<sc_physical_challenge, 3> challenges{};
};
struct campaign_row_campaign_row_payload {
    uint64_t revision{};
    uint32_t index{};
    uint32_t count{};
    sc_campaign_row row{};
    sc_campaign_summary summary{};
    std::vector<campaign_reward> rewards{};
};
struct campaign_commit_campaign_commit_payload {
    uint64_t revision{};
};
struct campaign_campaign_payload {
    uint64_t revision{};
};
struct inventory_observe_payload {
};
struct inventory_ensure_payload {
    uint32_t weapons{};
    uint32_t equipment{};
};
struct inventory_set_capacity_payload {
    uint8_t health_tier{};
    uint8_t armor_tier{};
    uint8_t ammo_tier{};
};
struct arsenal_observe_payload {
};
struct arsenal_ensure_mods_payload {
    uint32_t mods{};
};
struct arsenal_select_mod_payload {
    uint8_t weapon{};
    uint8_t mod{};
};
struct arsenal_project_mastery_payload {
    uint32_t masteries{};
};
struct runes_observe_payload {
};
struct runes_ensure_normal_payload {
    uint32_t normal_runes{};
};
struct runes_ensure_support_payload {
    uint32_t support_runes{};
};
struct runes_select_normal_payload {
    uint8_t slot{};
    int8_t rune{};
};
struct runes_select_support_payload {
    int8_t rune{};
};
struct special_observe_payload {
};
struct special_ensure_ownership_payload {
    uint32_t crucible{};
    uint32_t hammer{};
    uint32_t hammer_tier{};
};
struct special_select_payload {
    uint32_t selected{};
};
struct special_refill_status_payload {
};
struct special_refill_publish_payload {
    uint32_t balance{};
    uint32_t flags{};
};
struct special_refill_take_payload {
};
struct special_refill_execute_payload {
    uint64_t request_id{};
    uint32_t authorize{};
};
struct deathlink_observe_payload {
};
struct deathlink_configure_payload {
    uint32_t enabled{};
    uint32_t mode{};
};
struct deathlink_apply_remote_payload {
    uint64_t event_id{};
    std::array<uint8_t, 16> event_hash{};
};
struct deathlink_poll_local_payload {
};
struct deathlink_ack_local_payload {
    uint64_t sequence{};
};
struct deathlink_cancel_remote_payload {
    uint64_t event_id{};
};
struct automap_publish_payload {
    uint32_t known{};
    uint64_t revision{};
    std::array<uint64_t, 9> checked_locations{};
};
struct automap_observe_payload {
};
struct command_activate_payload {
    std::string entity{};
};
struct command_player_perk_payload {
    std::string perk{};
};
struct command_ammo_refill_payload {
};
struct command_crucible_refill_payload {
};
struct command_echo_payload {
    std::string text{};
};
struct command_condump_payload {
    std::string filename{};
};
struct command_list_inventory_payload {
};
struct command_transient_policy_payload {
    uint32_t variable{};
    std::string value{};
};
struct combat_map_identifier {
    std::string value{};
};
struct combat_combat_payload {
    std::vector<combat_map_identifier> maps{};
};
struct sc_diagnostic_request {
    sc_native_scope expected{};
    uint64_t request_id{};
    std::array<uint8_t, 16> nonce{};
    uint32_t deadline_ms{};
};
struct sc_combat_presentation_request {
    sc_diagnostic_request execution{};
    std::array<uint8_t, 65> namespace_id{};
    uint64_t total{};
    uint64_t target{};
    uint64_t first_milestone{};
    uint64_t milestone{};
    uint64_t milestone_step{};
    uint32_t visible{};
};
struct combat_presentation_combat_presentation_payload {
    sc_combat_presentation_request presentation{};
};
struct support_dump_support_dump_payload {
};
struct sc_diagnostic_detail {
    uint32_t revision{};
    uint32_t stage{};
    uint32_t observation_attempted{};
    uint32_t observation_accepted{};
    uint32_t timing_valid{};
    uint32_t timing_error{};
    uint32_t claim_lock_missed{};
    uint64_t observation_started_at_ms{};
    uint64_t observation_elapsed_ns{};
    uint64_t observation_budget_ns{};
    uint32_t sample_reason{};
    uint32_t state_validity{};
    uint32_t state_reason{};
    uint32_t state_error{};
    uint32_t map_validity{};
    uint32_t map_reason{};
    uint32_t map_error{};
    uint32_t pending_before{};
    uint32_t pending_before_reason{};
    uint32_t pending_before_error{};
    uint32_t pending_after{};
    uint32_t pending_after_reason{};
    uint32_t pending_after_error{};
};
struct diagnostic_detail_result {
    sc_diagnostic_result execution{};
    sc_diagnostic_detail detail{};
};
struct save_admission_result {
    sc_save_admission_snapshot observation{};
    std::array<uint8_t, 16> save_binding{};
};
struct campaign_row_base_payload {
    uint64_t revision{};
    uint32_t index{};
    uint32_t count{};
    sc_campaign_row row{};
};
struct campaign_row_presentation_payload {
    uint64_t revision{};
    uint32_t index{};
    uint32_t count{};
    sc_campaign_row row{};
    sc_campaign_summary summary{};
};
struct read_envelope {
    scope scope{};
    std::array<uint8_t, 64> namespace_id{};
    uint16_t command_type{};
    uint16_t payload_revision{};
    std::array<uint8_t, 16> save_binding{};
};
struct command_reply {
    std::array<uint8_t, 16> actual_instance_id{};
    command_reference reference{};
    result_dimensions dimensions{};
};
struct supported_command {
    uint16_t command_type{};
    uint16_t payload_revision{};
};
struct hello_result {
    scope scope{};
    std::vector<supported_command> supported{};
    uint32_t max_frame{};
    uint32_t execution_slots{};
    uint32_t full_result_ms{};
    uint32_t identity_capacity{};
};
struct immutable_submission {
    scope scope{};
    std::array<uint8_t, 64> namespace_id{};
    uint16_t command_type{};
    uint16_t payload_revision{};
    std::array<uint8_t, 16> save_binding{};
    uint32_t deadline_ms{};
};
struct command_response {
    command_reply reply{};
    uint32_t error{};
    sc_diagnostic_result execution{};
};
struct wire_header_1 {
    uint32_t magic{};
    uint16_t version{};
    uint16_t operation{};
    uint32_t payload_length{};
    uint32_t result{};
};
struct wire_header_2 {
    uint32_t magic{};
    uint16_t version{};
    uint16_t operation{};
    uint32_t payload_length{};
    uint32_t result{};
    uint64_t correlation_id{};
    uint32_t flags{};
    uint32_t reserved{};
};
using inspect_payload = std::variant<inspect_inspect_payload>;
using engine_payload = std::variant<engine_engine_payload>;
using context_payload = std::variant<context_context_payload>;
using native_payload = std::variant<native_native_payload>;
using diagnostic_payload = std::variant<diagnostic_diagnostic_payload>;
using diagnostic_detail_payload = std::variant<diagnostic_detail_diagnostic_detail_payload>;
using save_payload = std::variant<save_save_payload>;
using save_admission_payload = std::variant<save_admission_save_admission_payload>;
using save_write_payload = std::variant<save_write_save_write_payload>;
using save_backup_payload = std::variant<save_backup_save_backup_payload>;
using save_installation_payload = std::variant<save_installation_save_installation_payload>;
using weapon_points_payload = std::variant<weapon_points_observe_payload, weapon_points_grant_payload>;
using campaign_row_payload = std::variant<campaign_row_campaign_row_payload>;
using campaign_commit_payload = std::variant<campaign_commit_campaign_commit_payload>;
using campaign_payload = std::variant<campaign_campaign_payload>;
using inventory_payload = std::variant<inventory_observe_payload, inventory_ensure_payload, inventory_set_capacity_payload>;
using arsenal_payload = std::variant<arsenal_observe_payload, arsenal_ensure_mods_payload, arsenal_select_mod_payload, arsenal_project_mastery_payload>;
using runes_payload = std::variant<runes_observe_payload, runes_ensure_normal_payload, runes_ensure_support_payload, runes_select_normal_payload, runes_select_support_payload>;
using special_payload = std::variant<special_observe_payload, special_ensure_ownership_payload, special_select_payload, special_refill_status_payload, special_refill_publish_payload, special_refill_take_payload, special_refill_execute_payload>;
using deathlink_payload = std::variant<deathlink_observe_payload, deathlink_configure_payload, deathlink_apply_remote_payload, deathlink_poll_local_payload, deathlink_ack_local_payload, deathlink_cancel_remote_payload>;
using automap_payload = std::variant<automap_publish_payload, automap_observe_payload>;
using command_payload = std::variant<command_activate_payload, command_player_perk_payload, command_ammo_refill_payload, command_crucible_refill_payload, command_echo_payload, command_condump_payload, command_list_inventory_payload, command_transient_policy_payload>;
using combat_payload = std::variant<combat_combat_payload>;
using combat_presentation_payload = std::variant<combat_presentation_combat_presentation_payload>;
using support_dump_payload = std::variant<support_dump_support_dump_payload>;
using campaign_row_payload_revision1 = std::variant<campaign_row_base_payload>;
using campaign_row_payload_revision2 = std::variant<campaign_row_presentation_payload>;
bool encode(const v1_command_submit&, Bytes&);
bool decode(const Bytes&, v1_command_submit&);
bool encode(const v1_command_result&, Bytes&);
bool decode(const Bytes&, v1_command_result&);
bool encode(const v1_inventory_result&, Bytes&);
bool decode(const Bytes&, v1_inventory_result&);
bool encode(const v1_inspect_request&, Bytes&);
bool decode(const Bytes&, v1_inspect_request&);
bool encode(const v1_diagnostic_detail_submit&, Bytes&);
bool decode(const Bytes&, v1_diagnostic_detail_submit&);
bool encode(const campaign_reward&, Bytes&);
bool decode(const Bytes&, campaign_reward&);
bool encode(const v1_campaign_rewards_row&, Bytes&);
bool decode(const Bytes&, v1_campaign_rewards_row&);
bool encode(const v1_automap_publish&, Bytes&);
bool decode(const Bytes&, v1_automap_publish&);
bool encode(const v1_campaign_base&, Bytes&);
bool decode(const Bytes&, v1_campaign_base&);
bool encode(const v1_campaign_presentation&, Bytes&);
bool decode(const Bytes&, v1_campaign_presentation&);
bool encode(const v1_runes_request&, Bytes&);
bool decode(const Bytes&, v1_runes_request&);
bool encode(const scope&, Bytes&);
bool decode(const Bytes&, scope&);
bool encode(const command_reference&, Bytes&);
bool decode(const Bytes&, command_reference&);
bool encode(const submission_identity&, Bytes&);
bool decode(const Bytes&, submission_identity&);
bool encode(const submission&, Bytes&);
bool decode(const Bytes&, submission&);
bool encode(const ack&, Bytes&);
bool decode(const Bytes&, ack&);
bool encode(const result_dimensions&, Bytes&);
bool decode(const Bytes&, result_dimensions&);
bool encode(const sc_status&, Bytes&);
bool decode(const Bytes&, sc_status&);
bool encode(const sc_engine_field&, Bytes&);
bool decode(const Bytes&, sc_engine_field&);
bool encode(const sc_engine_snapshot&, Bytes&);
bool decode(const Bytes&, sc_engine_snapshot&);
bool encode(const sc_context_map&, Bytes&);
bool decode(const Bytes&, sc_context_map&);
bool encode(const sc_context_field&, Bytes&);
bool decode(const Bytes&, sc_context_field&);
bool encode(const sc_context_snapshot&, Bytes&);
bool decode(const Bytes&, sc_context_snapshot&);
bool encode(const sc_native_scope&, Bytes&);
bool decode(const Bytes&, sc_native_scope&);
bool encode(const sc_native_event&, Bytes&);
bool decode(const Bytes&, sc_native_event&);
bool encode(const sc_native_snapshot&, Bytes&);
bool decode(const Bytes&, sc_native_snapshot&);
bool encode(const sc_diagnostic_result&, Bytes&);
bool decode(const Bytes&, sc_diagnostic_result&);
bool encode(const sc_save_field&, Bytes&);
bool decode(const Bytes&, sc_save_field&);
bool encode(const sc_save_snapshot&, Bytes&);
bool decode(const Bytes&, sc_save_snapshot&);
bool encode(const sc_save_admission_snapshot&, Bytes&);
bool decode(const Bytes&, sc_save_admission_snapshot&);
bool encode(const sc_save_write_snapshot&, Bytes&);
bool decode(const Bytes&, sc_save_write_snapshot&);
bool encode(const sc_save_backup_snapshot&, Bytes&);
bool decode(const Bytes&, sc_save_backup_snapshot&);
bool encode(const sc_install_event&, Bytes&);
bool decode(const Bytes&, sc_install_event&);
bool encode(const sc_save_installation_snapshot&, Bytes&);
bool decode(const Bytes&, sc_save_installation_snapshot&);
bool encode(const sc_weapon_points_result&, Bytes&);
bool decode(const Bytes&, sc_weapon_points_result&);
bool encode(const sc_campaign_result&, Bytes&);
bool decode(const Bytes&, sc_campaign_result&);
bool encode(const sc_inventory_result&, Bytes&);
bool decode(const Bytes&, sc_inventory_result&);
bool encode(const sc_arsenal_result&, Bytes&);
bool decode(const Bytes&, sc_arsenal_result&);
bool encode(const sc_runes_result&, Bytes&);
bool decode(const Bytes&, sc_runes_result&);
bool encode(const sc_special_result&, Bytes&);
bool decode(const Bytes&, sc_special_result&);
bool encode(const sc_deathlink_result&, Bytes&);
bool decode(const Bytes&, sc_deathlink_result&);
bool encode(const sc_automap_result&, Bytes&);
bool decode(const Bytes&, sc_automap_result&);
bool encode(const sc_command_result&, Bytes&);
bool decode(const Bytes&, sc_command_result&);
bool encode(const sc_combat_snapshot&, Bytes&);
bool decode(const Bytes&, sc_combat_snapshot&);
bool encode(const sc_combat_presentation_result&, Bytes&);
bool decode(const Bytes&, sc_combat_presentation_result&);
bool encode(const inspect_inspect_payload&, Bytes&);
bool decode(const Bytes&, inspect_inspect_payload&);
bool encode(const engine_engine_payload&, Bytes&);
bool decode(const Bytes&, engine_engine_payload&);
bool encode(const context_context_payload&, Bytes&);
bool decode(const Bytes&, context_context_payload&);
bool encode(const native_native_payload&, Bytes&);
bool decode(const Bytes&, native_native_payload&);
bool encode(const diagnostic_diagnostic_payload&, Bytes&);
bool decode(const Bytes&, diagnostic_diagnostic_payload&);
bool encode(const diagnostic_detail_diagnostic_detail_payload&, Bytes&);
bool decode(const Bytes&, diagnostic_detail_diagnostic_detail_payload&);
bool encode(const save_save_payload&, Bytes&);
bool decode(const Bytes&, save_save_payload&);
bool encode(const save_admission_save_admission_payload&, Bytes&);
bool decode(const Bytes&, save_admission_save_admission_payload&);
bool encode(const save_write_save_write_payload&, Bytes&);
bool decode(const Bytes&, save_write_save_write_payload&);
bool encode(const save_backup_save_backup_payload&, Bytes&);
bool decode(const Bytes&, save_backup_save_backup_payload&);
bool encode(const save_installation_save_installation_payload&, Bytes&);
bool decode(const Bytes&, save_installation_save_installation_payload&);
bool encode(const weapon_points_observe_payload&, Bytes&);
bool decode(const Bytes&, weapon_points_observe_payload&);
bool encode(const weapon_points_grant_payload&, Bytes&);
bool decode(const Bytes&, weapon_points_grant_payload&);
bool encode(const sc_campaign_row&, Bytes&);
bool decode(const Bytes&, sc_campaign_row&);
bool encode(const sc_physical_challenge&, Bytes&);
bool decode(const Bytes&, sc_physical_challenge&);
bool encode(const sc_campaign_summary&, Bytes&);
bool decode(const Bytes&, sc_campaign_summary&);
bool encode(const campaign_row_campaign_row_payload&, Bytes&);
bool decode(const Bytes&, campaign_row_campaign_row_payload&);
bool encode(const campaign_commit_campaign_commit_payload&, Bytes&);
bool decode(const Bytes&, campaign_commit_campaign_commit_payload&);
bool encode(const campaign_campaign_payload&, Bytes&);
bool decode(const Bytes&, campaign_campaign_payload&);
bool encode(const inventory_observe_payload&, Bytes&);
bool decode(const Bytes&, inventory_observe_payload&);
bool encode(const inventory_ensure_payload&, Bytes&);
bool decode(const Bytes&, inventory_ensure_payload&);
bool encode(const inventory_set_capacity_payload&, Bytes&);
bool decode(const Bytes&, inventory_set_capacity_payload&);
bool encode(const arsenal_observe_payload&, Bytes&);
bool decode(const Bytes&, arsenal_observe_payload&);
bool encode(const arsenal_ensure_mods_payload&, Bytes&);
bool decode(const Bytes&, arsenal_ensure_mods_payload&);
bool encode(const arsenal_select_mod_payload&, Bytes&);
bool decode(const Bytes&, arsenal_select_mod_payload&);
bool encode(const arsenal_project_mastery_payload&, Bytes&);
bool decode(const Bytes&, arsenal_project_mastery_payload&);
bool encode(const runes_observe_payload&, Bytes&);
bool decode(const Bytes&, runes_observe_payload&);
bool encode(const runes_ensure_normal_payload&, Bytes&);
bool decode(const Bytes&, runes_ensure_normal_payload&);
bool encode(const runes_ensure_support_payload&, Bytes&);
bool decode(const Bytes&, runes_ensure_support_payload&);
bool encode(const runes_select_normal_payload&, Bytes&);
bool decode(const Bytes&, runes_select_normal_payload&);
bool encode(const runes_select_support_payload&, Bytes&);
bool decode(const Bytes&, runes_select_support_payload&);
bool encode(const special_observe_payload&, Bytes&);
bool decode(const Bytes&, special_observe_payload&);
bool encode(const special_ensure_ownership_payload&, Bytes&);
bool decode(const Bytes&, special_ensure_ownership_payload&);
bool encode(const special_select_payload&, Bytes&);
bool decode(const Bytes&, special_select_payload&);
bool encode(const special_refill_status_payload&, Bytes&);
bool decode(const Bytes&, special_refill_status_payload&);
bool encode(const special_refill_publish_payload&, Bytes&);
bool decode(const Bytes&, special_refill_publish_payload&);
bool encode(const special_refill_take_payload&, Bytes&);
bool decode(const Bytes&, special_refill_take_payload&);
bool encode(const special_refill_execute_payload&, Bytes&);
bool decode(const Bytes&, special_refill_execute_payload&);
bool encode(const deathlink_observe_payload&, Bytes&);
bool decode(const Bytes&, deathlink_observe_payload&);
bool encode(const deathlink_configure_payload&, Bytes&);
bool decode(const Bytes&, deathlink_configure_payload&);
bool encode(const deathlink_apply_remote_payload&, Bytes&);
bool decode(const Bytes&, deathlink_apply_remote_payload&);
bool encode(const deathlink_poll_local_payload&, Bytes&);
bool decode(const Bytes&, deathlink_poll_local_payload&);
bool encode(const deathlink_ack_local_payload&, Bytes&);
bool decode(const Bytes&, deathlink_ack_local_payload&);
bool encode(const deathlink_cancel_remote_payload&, Bytes&);
bool decode(const Bytes&, deathlink_cancel_remote_payload&);
bool encode(const automap_publish_payload&, Bytes&);
bool decode(const Bytes&, automap_publish_payload&);
bool encode(const automap_observe_payload&, Bytes&);
bool decode(const Bytes&, automap_observe_payload&);
bool encode(const command_activate_payload&, Bytes&);
bool decode(const Bytes&, command_activate_payload&);
bool encode(const command_player_perk_payload&, Bytes&);
bool decode(const Bytes&, command_player_perk_payload&);
bool encode(const command_ammo_refill_payload&, Bytes&);
bool decode(const Bytes&, command_ammo_refill_payload&);
bool encode(const command_crucible_refill_payload&, Bytes&);
bool decode(const Bytes&, command_crucible_refill_payload&);
bool encode(const command_echo_payload&, Bytes&);
bool decode(const Bytes&, command_echo_payload&);
bool encode(const command_condump_payload&, Bytes&);
bool decode(const Bytes&, command_condump_payload&);
bool encode(const command_list_inventory_payload&, Bytes&);
bool decode(const Bytes&, command_list_inventory_payload&);
bool encode(const command_transient_policy_payload&, Bytes&);
bool decode(const Bytes&, command_transient_policy_payload&);
bool encode(const combat_map_identifier&, Bytes&);
bool decode(const Bytes&, combat_map_identifier&);
bool encode(const combat_combat_payload&, Bytes&);
bool decode(const Bytes&, combat_combat_payload&);
bool encode(const sc_diagnostic_request&, Bytes&);
bool decode(const Bytes&, sc_diagnostic_request&);
bool encode(const sc_combat_presentation_request&, Bytes&);
bool decode(const Bytes&, sc_combat_presentation_request&);
bool encode(const combat_presentation_combat_presentation_payload&, Bytes&);
bool decode(const Bytes&, combat_presentation_combat_presentation_payload&);
bool encode(const support_dump_support_dump_payload&, Bytes&);
bool decode(const Bytes&, support_dump_support_dump_payload&);
bool encode(const sc_diagnostic_detail&, Bytes&);
bool decode(const Bytes&, sc_diagnostic_detail&);
bool encode(const diagnostic_detail_result&, Bytes&);
bool decode(const Bytes&, diagnostic_detail_result&);
bool encode(const save_admission_result&, Bytes&);
bool decode(const Bytes&, save_admission_result&);
bool encode(const campaign_row_base_payload&, Bytes&);
bool decode(const Bytes&, campaign_row_base_payload&);
bool encode(const campaign_row_presentation_payload&, Bytes&);
bool decode(const Bytes&, campaign_row_presentation_payload&);
bool encode(const read_envelope&, Bytes&);
bool decode(const Bytes&, read_envelope&);
bool encode(const command_reply&, Bytes&);
bool decode(const Bytes&, command_reply&);
bool encode(const supported_command&, Bytes&);
bool decode(const Bytes&, supported_command&);
bool encode(const hello_result&, Bytes&);
bool decode(const Bytes&, hello_result&);
bool encode(const immutable_submission&, Bytes&);
bool decode(const Bytes&, immutable_submission&);
bool encode(const command_response&, Bytes&);
bool decode(const Bytes&, command_response&);
bool encode(const wire_header_1&, Bytes&);
bool decode(const Bytes&, wire_header_1&);
bool encode(const wire_header_2&, Bytes&);
bool decode(const Bytes&, wire_header_2&);
bool encode(const inspect_payload&, Bytes&);
bool decode(const Bytes&, inspect_payload&);
bool encode(const engine_payload&, Bytes&);
bool decode(const Bytes&, engine_payload&);
bool encode(const context_payload&, Bytes&);
bool decode(const Bytes&, context_payload&);
bool encode(const native_payload&, Bytes&);
bool decode(const Bytes&, native_payload&);
bool encode(const diagnostic_payload&, Bytes&);
bool decode(const Bytes&, diagnostic_payload&);
bool encode(const diagnostic_detail_payload&, Bytes&);
bool decode(const Bytes&, diagnostic_detail_payload&);
bool encode(const save_payload&, Bytes&);
bool decode(const Bytes&, save_payload&);
bool encode(const save_admission_payload&, Bytes&);
bool decode(const Bytes&, save_admission_payload&);
bool encode(const save_write_payload&, Bytes&);
bool decode(const Bytes&, save_write_payload&);
bool encode(const save_backup_payload&, Bytes&);
bool decode(const Bytes&, save_backup_payload&);
bool encode(const save_installation_payload&, Bytes&);
bool decode(const Bytes&, save_installation_payload&);
bool encode(const weapon_points_payload&, Bytes&);
bool decode(const Bytes&, weapon_points_payload&);
bool encode(const campaign_row_payload&, Bytes&);
bool decode(const Bytes&, campaign_row_payload&);
bool encode(const campaign_commit_payload&, Bytes&);
bool decode(const Bytes&, campaign_commit_payload&);
bool encode(const campaign_payload&, Bytes&);
bool decode(const Bytes&, campaign_payload&);
bool encode(const inventory_payload&, Bytes&);
bool decode(const Bytes&, inventory_payload&);
bool encode(const arsenal_payload&, Bytes&);
bool decode(const Bytes&, arsenal_payload&);
bool encode(const runes_payload&, Bytes&);
bool decode(const Bytes&, runes_payload&);
bool encode(const special_payload&, Bytes&);
bool decode(const Bytes&, special_payload&);
bool encode(const deathlink_payload&, Bytes&);
bool decode(const Bytes&, deathlink_payload&);
bool encode(const automap_payload&, Bytes&);
bool decode(const Bytes&, automap_payload&);
bool encode(const command_payload&, Bytes&);
bool decode(const Bytes&, command_payload&);
bool encode(const combat_payload&, Bytes&);
bool decode(const Bytes&, combat_payload&);
bool encode(const combat_presentation_payload&, Bytes&);
bool decode(const Bytes&, combat_presentation_payload&);
bool encode(const support_dump_payload&, Bytes&);
bool decode(const Bytes&, support_dump_payload&);
bool encode(const campaign_row_payload_revision1&, Bytes&);
bool decode(const Bytes&, campaign_row_payload_revision1&);
bool encode(const campaign_row_payload_revision2&, Bytes&);
bool decode(const Bytes&, campaign_row_payload_revision2&);
bool encode_command_submit(const v1_command_submit&, Bytes&, uint32_t result = 0);
bool decode_command_submit(const Bytes&, v1_command_submit&, uint32_t& result);
bool encode_command_result(const v1_command_result&, Bytes&, uint32_t result = 0);
bool decode_command_result(const Bytes&, v1_command_result&, uint32_t& result);
bool encode_inventory_result(const v1_inventory_result&, Bytes&, uint32_t result = 0);
bool decode_inventory_result(const Bytes&, v1_inventory_result&, uint32_t& result);
bool encode_inspect_request(const v1_inspect_request&, Bytes&, uint32_t result = 0);
bool decode_inspect_request(const Bytes&, v1_inspect_request&, uint32_t& result);
bool encode_diagnostic_detail_submit(const v1_diagnostic_detail_submit&, Bytes&, uint32_t result = 0);
bool decode_diagnostic_detail_submit(const Bytes&, v1_diagnostic_detail_submit&, uint32_t& result);
bool encode_campaign_rewards_row(const v1_campaign_rewards_row&, Bytes&, uint32_t result = 0);
bool decode_campaign_rewards_row(const Bytes&, v1_campaign_rewards_row&, uint32_t& result);
bool encode_automap_publish(const v1_automap_publish&, Bytes&, uint32_t result = 0);
bool decode_automap_publish(const Bytes&, v1_automap_publish&, uint32_t& result);
bool encode_campaign_base(const v1_campaign_base&, Bytes&, uint32_t result = 0);
bool decode_campaign_base(const Bytes&, v1_campaign_base&, uint32_t& result);
bool encode_campaign_presentation(const v1_campaign_presentation&, Bytes&, uint32_t result = 0);
bool decode_campaign_presentation(const Bytes&, v1_campaign_presentation&, uint32_t& result);
bool encode_runes_request(const v1_runes_request&, Bytes&, uint32_t result = 0);
bool decode_runes_request(const Bytes&, v1_runes_request&, uint32_t& result);
template<class C, class T> void v1_transfer_envelope(C& c, T& v) {
    c.scalar(v.expected.pid, 4);
    c.scalar(v.expected.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.expected.instance_id[i], 1);
    c.scalar(v.expected.lifecycle_generation, 8);
    c.scalar(v.request_id, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.nonce[i], 1);
    c.scalar(v.deadline_ms, 4);
}
template<class C, class T> void v1_transfer_backup_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.campaign, 4);
    c.scalar(v.slot, 4);
    c.scalar(v.work_deadline_ms, 4);
}
template<class C, class T> void v1_transfer_points_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.amount, 4);
    c.scalar(v.expected_gained, 4);
}
template<class C, class T> void v1_transfer_inventory_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.weapons, 4);
    c.scalar(v.equipment, 4);
    c.scalar(v.special_weapons, 4);
    c.scalar(v.persistent_upgrades, 4);
    c.scalar(v.health_tier, 1);
    c.scalar(v.armor_tier, 1);
    c.scalar(v.ammo_tier, 1);
    c.scalar(v.reserved, 1);
}
template<class C, class T> void v1_transfer_arsenal_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.mods, 4);
    c.scalar(v.upgrades, 4);
    c.scalar(v.masteries, 4);
    c.scalar(v.select_weapon, 1);
    c.scalar(v.select_mod, 1);
    c.scalar(v.challenge_index, 2);
    c.scalar(v.challenge_progress, 4);
    c.scalar(v.challenge_completed, 1);
    for (size_t i = 0; i < 3; ++i) c.scalar(v.reserved[i], 1);
}
template<class C, class T> void v1_transfer_runes_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.normal_runes, 4);
    c.scalar(v.support_runes, 4);
    c.scalar(v.select_slot, 1);
    c.scalar(v.select_rune, 1);
    for (size_t i = 0; i < 2; ++i) c.scalar(v.reserved[i], 1);
}
template<class C, class T> void v1_transfer_special_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.own_crucible, 4);
    c.scalar(v.own_hammer, 4);
    c.scalar(v.hammer_tier, 4);
    c.scalar(v.selected, 4);
    c.scalar(v.refill_balance, 4);
    c.scalar(v.refill_flags, 4);
    c.scalar(v.refill_request_id, 8);
    c.scalar(v.refill_authorize, 4);
    c.scalar(v.reserved0, 4);
}
template<class C, class T> void v1_transfer_deathlink_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.enabled, 4);
    c.scalar(v.mode, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.event_id, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.event_hash[i], 1);
    c.scalar(v.ack_sequence, 8);
    c.scalar(v.reserved0, 4);
}
template<class C, class T> void v1_transfer_automap_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.known, 4);
    c.scalar(v.revision, 8);
    for (size_t i = 0; i < 9; ++i) c.scalar(v.checked_locations[i], 8);
}
template<class C, class T> void v1_transfer_command_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    for (size_t i = 0; i < 2048; ++i) c.scalar(v.text[i], 1);
}
template<class C, class T> void v1_transfer_presentation_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.total, 8);
    c.scalar(v.target, 8);
    c.scalar(v.first_milestone, 8);
    c.scalar(v.milestone, 8);
    c.scalar(v.milestone_step, 8);
    c.scalar(v.visible, 4);
}
template<class C, class T> void v1_transfer_campaign_request(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.revision, 8);
    c.scalar(v.index, 4);
    c.scalar(v.count, 4);
    c.scalar(v.row.id, 4);
    c.scalar(v.row.flags, 4);
    c.scalar(v.row.native_index, 4);
    for (size_t i = 0; i < 192; ++i) c.scalar(v.row.map[i], 1);
    for (size_t i = 0; i < 96; ++i) c.scalar(v.row.title[i], 1);
}
template<class C, class T> void v1_transfer_campaign_summary(C& c, T& v) {
    c.scalar(v.known, 4);
    c.scalar(v.found, 4);
    c.scalar(v.total, 4);
    c.scalar(v.band, 4);
}
template<class C, class T> void v1_transfer_campaign_challenge(C& c, T& v) {
    for (size_t i = 0; i < 80; ++i) c.scalar(v.unlockable[i], 1);
    c.scalar(v.found, 4);
    c.scalar(v.required, 4);
    c.scalar(v.checked, 4);
}
template<class C, class T> void v1_transfer_campaign_reward(C& c, T& v) {
    c.scalar(v.location_id, 4);
    c.scalar(v.kind, 4);
    c.scalar(v.checked, 4);
    for (size_t i = 0; i < 80; ++i) c.scalar(v.unlockable[i], 1);
    for (size_t i = 0; i < 128; ++i) c.scalar(v.name[i], 1);
    for (size_t i = 0; i < 512; ++i) c.scalar(v.text[i], 1);
}
template<class C, class T> void v1_transfer_response_identity(C& c, T& v) {
    c.scalar(v.pid, 4);
    c.scalar(v.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.instance[i], 1);
    c.scalar(v.core.abi_version, 4);
    c.text(v.core.version);
    c.text(v.core.build_id);
}
template<class C, class T> void v1_transfer_points_result_tail(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.amount, 4);
    c.scalar(v.expected_gained, 4);
    c.scalar(v.outcome, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.native_exception, 4);
    c.scalar(v.balance_before, 4);
    c.scalar(v.balance_after, 4);
    c.scalar(v.gained_before, 4);
    c.scalar(v.gained_after, 4);
    c.scalar(v.suppressed_grants, 8);
}
template<class C, class T> void v1_transfer_inventory_result_tail(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.outcome, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.native_exception, 4);
    c.scalar(v.weapons_before, 4);
    c.scalar(v.weapons_after, 4);
    c.scalar(v.equipment_before, 4);
    c.scalar(v.equipment_after, 4);
    c.scalar(v.special_before, 4);
    c.scalar(v.special_after, 4);
    c.scalar(v.upgrades_before, 4);
    c.scalar(v.upgrades_after, 4);
    c.scalar(v.health_tier_before, 1);
    c.scalar(v.health_tier_after, 1);
    c.scalar(v.armor_tier_before, 1);
    c.scalar(v.armor_tier_after, 1);
    c.scalar(v.ammo_tier_before, 1);
    c.scalar(v.ammo_tier_after, 1);
    c.scalar(v.reserved_before, 1);
    c.scalar(v.reserved_after, 1);
    c.scalar(v.operations_applied, 8);
    c.scalar(v.ice_bomb_before, 1);
    c.scalar(v.ice_bomb_after, 1);
    c.scalar(v.dash_before, 1);
    c.scalar(v.dash_after, 1);
    c.scalar(v.slayer_key_before, 1);
    c.scalar(v.slayer_key_after, 1);
    c.scalar(v.frag_grenade_before, 1);
    c.scalar(v.frag_grenade_after, 1);
    c.scalar(v.flame_belch_before, 1);
    c.scalar(v.flame_belch_after, 1);
    c.scalar(v.blood_punch_before, 1);
    c.scalar(v.blood_punch_after, 1);
    c.scalar(v.perks_known, 8);
    c.scalar(v.perks_owned, 8);
}
template<class C, class T> void v1_transfer_arsenal_result_tail(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.outcome, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.native_exception, 4);
    c.scalar(v.weapons_before, 4);
    c.scalar(v.weapons_after, 4);
    c.scalar(v.mods_before, 4);
    c.scalar(v.mods_after, 4);
    for (size_t i = 0; i < 8; ++i) c.scalar(v.selected_mods_before[i], 1);
    for (size_t i = 0; i < 8; ++i) c.scalar(v.selected_mods_after[i], 1);
    c.scalar(v.normal_upgrades_before, 4);
    c.scalar(v.normal_upgrades_after, 4);
    c.scalar(v.masteries_ap_before, 2);
    c.scalar(v.masteries_ap_after, 2);
    c.scalar(v.mastery_challenges_active_before, 2);
    c.scalar(v.mastery_challenges_active_after, 2);
    c.scalar(v.mastery_challenges_completed_before, 2);
    c.scalar(v.mastery_challenges_completed_after, 2);
    c.scalar(v.masteries_effective_before, 2);
    c.scalar(v.masteries_effective_after, 2);
    c.scalar(v.mission_challenges_active_before, 4);
    c.scalar(v.mission_challenges_active_after, 4);
    c.scalar(v.mission_challenges_completed_before, 4);
    c.scalar(v.mission_challenges_completed_after, 4);
    c.scalar(v.operations_applied, 8);
}
template<class C, class T> void v1_transfer_runes_result_tail(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.outcome, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.native_exception, 4);
    c.scalar(v.owned_normal_before, 4);
    c.scalar(v.owned_normal_after, 4);
    c.scalar(v.owned_support_before, 4);
    c.scalar(v.owned_support_after, 4);
    for (size_t i = 0; i < 3; ++i) c.scalar(v.selected_slots_before[i], 1);
    for (size_t i = 0; i < 3; ++i) c.scalar(v.selected_slots_after[i], 1);
    c.scalar(v.selected_support_before, 1);
    c.scalar(v.selected_support_after, 1);
    c.scalar(v.unlocked_slots_before, 1);
    c.scalar(v.unlocked_slots_after, 1);
    c.scalar(v.derived_pairs_before, 1);
    c.scalar(v.derived_pairs_after, 1);
    c.scalar(v.reserved_before, 1);
    c.scalar(v.reserved_after, 1);
    c.scalar(v.operations_applied, 8);
}
template<class C, class T> void v1_transfer_special_result_tail(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.outcome, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.native_exception, 4);
    c.scalar(v.owns_crucible, 4);
    c.scalar(v.owns_hammer, 4);
    c.scalar(v.hammer_tier, 4);
    c.scalar(v.selected, 4);
    c.scalar(v.native_crucible, 4);
    c.scalar(v.native_hammer, 4);
    c.scalar(v.native_hammer_perks, 4);
    c.scalar(v.native_selected, 4);
    c.scalar(v.native_state_known, 4);
    c.scalar(v.crucible_charge, 4);
    c.scalar(v.crucible_charge_max, 4);
    c.scalar(v.refill_balance, 4);
    c.scalar(v.refill_flags, 4);
    c.scalar(v.refill_request_id, 8);
    c.scalar(v.refill_request_state, 4);
    c.scalar(v.refill_executed, 4);
    c.scalar(v.refill_execution_id, 8);
    c.scalar(v.refill_sequence, 4);
    c.scalar(v.reserved0, 4);
    c.scalar(v.operations_applied, 8);
}
template<class C, class T> void v1_transfer_deathlink_result_tail(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.outcome, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.native_exception, 4);
    c.scalar(v.enabled, 4);
    c.scalar(v.mode, 4);
    c.scalar(v.remote_state, 4);
    c.scalar(v.remote_protection, 4);
    c.scalar(v.remote_attempts, 4);
    c.scalar(v.pending_count, 4);
    c.scalar(v.remote_event_id, 8);
    c.scalar(v.local_candidates, 4);
    c.scalar(v.local_suppressed, 4);
    c.scalar(v.local_state, 4);
    c.scalar(v.local_death_sequence, 8);
    c.scalar(v.local_death_time_ms, 8);
    c.scalar(v.local_cause, 4);
    c.scalar(v.local_protection, 4);
    c.scalar(v.local_flags, 4);
    c.scalar(v.reserved0, 4);
    c.scalar(v.operations_applied, 8);
}
template<class C, class T> void v1_transfer_backup_result_tail(C& c, T& v) {
    c.scalar(v.operation_id, 8);
    c.scalar(v.flags, 4);
    c.scalar(v.native_exception, 4);
    c.scalar(v.storage_outcome, 4);
    c.scalar(v.storage_error, 4);
    c.scalar(v.files, 4);
    c.scalar(v.bytes, 8);
    for (size_t i = 0; i < 80; ++i) c.scalar(v.basename[i], 1);
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.campaign, 4);
    c.scalar(v.slot, 4);
}
template<class C, class T> void v1_transfer_command_result_tail(C& c, T& v) {
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.outcome, 4);
    c.scalar(v.native_exception, 4);
}
template<class C, class T> void v1_transfer_scope(C& c, T& v) {
    c.scalar(v.pid, 4);
    c.scalar(v.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.instance_id[i], 1);
    c.scalar(v.lifecycle_generation, 8);
}
template<class C, class T> void v1_transfer_diagnostic_result(C& c, T& v) {
    c.scalar(v.state, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.cancel_requested, 4);
    c.scalar(v.retrieved, 4);
    c.scalar(v.scope.pid, 4);
    c.scalar(v.scope.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.scope.instance_id[i], 1);
    c.scalar(v.scope.lifecycle_generation, 8);
    c.scalar(v.request_id, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.nonce[i], 1);
    c.scalar(v.admitted_at_ms, 8);
    c.scalar(v.deadline_at_ms, 8);
    c.scalar(v.claimed_at_ms, 8);
    c.scalar(v.observed_at_ms, 8);
    c.scalar(v.executed_at_ms, 8);
    c.scalar(v.completed_at_ms, 8);
    c.scalar(v.retrieved_at_ms, 8);
    c.scalar(v.thread_id, 4);
    c.scalar(v.site_revision, 4);
    c.scalar(v.phase, 4);
    c.scalar(v.lifecycle, 4);
    c.scalar(v.game_state, 4);
}
template<class C, class T> void v1_transfer_detail(C& c, T& v) {
    c.scalar(v.revision, 4);
    c.scalar(v.stage, 4);
    c.scalar(v.observation_attempted, 4);
    c.scalar(v.observation_accepted, 4);
    c.scalar(v.timing_valid, 4);
    c.scalar(v.timing_error, 4);
    c.scalar(v.claim_lock_missed, 4);
    c.scalar(v.observation_started_at_ms, 8);
    c.scalar(v.observation_elapsed_ns, 8);
    c.scalar(v.observation_budget_ns, 8);
    c.scalar(v.sample_reason, 4);
    c.scalar(v.state_validity, 4);
    c.scalar(v.state_reason, 4);
    c.scalar(v.state_error, 4);
    c.scalar(v.map_validity, 4);
    c.scalar(v.map_reason, 4);
    c.scalar(v.map_error, 4);
    c.scalar(v.pending_before, 4);
    c.scalar(v.pending_before_reason, 4);
    c.scalar(v.pending_before_error, 4);
    c.scalar(v.pending_after, 4);
    c.scalar(v.pending_after_reason, 4);
    c.scalar(v.pending_after_error, 4);
}
template<class C, class T> void v1_transfer_automap_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.scope.pid, 4);
    c.scalar(v.scope.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.scope.instance_id[i], 1);
    c.scalar(v.scope.lifecycle_generation, 8);
    c.scalar(v.request_id, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.nonce[i], 1);
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.kind, 4);
    c.scalar(v.outcome, 4);
    c.scalar(v.known, 4);
    c.scalar(v.native_fault, 4);
    c.scalar(v.revision, 8);
    c.scalar(v.scanned, 8);
    c.scalar(v.collected, 8);
    c.scalar(v.completed_passes, 8);
}
template<class C, class T> void v1_transfer_presentation_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.scope.pid, 4);
    c.scalar(v.scope.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.scope.instance_id[i], 1);
    c.scalar(v.scope.lifecycle_generation, 8);
    c.scalar(v.request_id, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.nonce[i], 1);
    c.scalar(v.status, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.total, 8);
    c.scalar(v.target, 8);
    c.scalar(v.milestone, 8);
    c.scalar(v.visible, 4);
}
template<class C, class T> void v1_transfer_campaign_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.scope.pid, 4);
    c.scalar(v.scope.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.scope.instance_id[i], 1);
    c.scalar(v.scope.lifecycle_generation, 8);
    c.scalar(v.request_id, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.nonce[i], 1);
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    c.scalar(v.status, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.committed_revision, 8);
    c.scalar(v.rendered_revision, 8);
    c.scalar(v.selected_id, 4);
    c.scalar(v.loaded_id, 4);
}
template<class C, class T> void v1_transfer_combat_result(C& c, T& v) {
    c.scalar(v.scope.pid, 4);
    c.scalar(v.scope.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.scope.instance_id[i], 1);
    c.scalar(v.scope.lifecycle_generation, 8);
    c.scalar(v.availability, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.observation_gaps, 8);
    c.text(v.namespace_id);
    c.scalar(v.qualified_kills, 8);
}
template<class C, class T> void v1_transfer_engine_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.sequence, 8);
    c.scalar(v.sampled_at_ms, 8);
    c.scalar(v.duration_ms, 4);
    c.scalar(v.sample_reason, 4);
    c.scalar(v.pe_reason, 4);
    c.scalar(v.machine, 4);
    c.scalar(v.timestamp, 4);
    c.scalar(v.image_size, 4);
    c.scalar(v.entry_rva, 4);
    c.scalar(v.disk_hash_reason, 4);
    for (size_t i = 0; i < 64; ++i) c.scalar(v.disk_sha256[i], 1);
    c.scalar(v.profile, 4);
    c.scalar(v.locator_revision, 4);
    c.scalar(v.root_locator_reason, 4);
    c.scalar(v.root_signature_rva, 4);
    c.scalar(v.root_target_rva, 4);
    c.scalar(v.profile_runtime_validated, 4);
}
template<class C, class T> void v1_transfer_engine_field(C& c, T& v) {
    c.scalar(v.validity, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.value, 4);
    c.scalar(v.win32_error, 4);
}
template<class C, class T> void v1_transfer_save_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.profile, 4);
    c.scalar(v.sequence, 8);
    c.scalar(v.sampled_at_ms, 8);
    c.scalar(v.duration_ms, 4);
    c.scalar(v.sample_reason, 4);
    c.scalar(v.layout_revision, 4);
    c.scalar(v.root_locator_reason, 4);
    c.scalar(v.mutation_available, 4);
    c.scalar(v.mutation_reason, 4);
}
template<class C, class T> void v1_transfer_save_field(C& c, T& v) {
    c.scalar(v.validity, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.value, 8);
    c.scalar(v.win32_error, 4);
}
template<class C, class T> void v1_transfer_admission_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.state, 4);
    c.scalar(v.fault, 4);
    c.scalar(v.prepared_routes, 4);
    c.scalar(v.required_routes, 4);
    c.scalar(v.flags, 4);
    for (size_t i = 0; i < 65; ++i) c.scalar(v.namespace_id[i], 1);
    for (size_t i = 0; i < 44; ++i) c.scalar(v.native_root[i], 1);
}
template<class C, class T> void v1_transfer_write_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.state, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.operation_id, 8);
    c.scalar(v.sdk_sequence, 8);
    c.scalar(v.file_count, 4);
    c.scalar(v.submitted, 4);
    c.scalar(v.completed, 4);
    c.scalar(v.pending_handles, 4);
    c.scalar(v.preparation_jobs, 4);
    c.scalar(v.preflight_jobs, 4);
    c.scalar(v.native_value, 4);
    c.scalar(v.reserved, 4);
    c.scalar(v.native_state, 8);
    c.scalar(v.native_outcome, 8);
    for (size_t i = 0; i < 64; ++i) c.scalar(v.directory[i], 1);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.reserved_bytes[i], 1);
}
template<class C, class T> void v1_transfer_installation_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.attempt, 4);
    c.scalar(v.phase, 4);
    c.scalar(v.last_completed_stage, 4);
    c.scalar(v.startup_observation, 4);
    c.scalar(v.validated, 4);
    c.scalar(v.created, 4);
    c.scalar(v.enabled, 4);
    c.scalar(v.cleanup_failures, 4);
    c.scalar(v.gaps, 4);
    c.scalar(v.sequence, 8);
}
template<class C, class T> void v1_transfer_installation_event(C& c, T& v) {
    c.scalar(v.sequence, 8);
    c.scalar(v.at_ms, 8);
    c.scalar(v.duration_ms, 8);
    c.scalar(v.stage, 4);
    c.scalar(v.target_group, 4);
    c.scalar(v.target_index, 4);
    c.scalar(v.rva, 4);
    c.scalar(v.signature_offset, 4);
    c.scalar(v.result, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.read_reason, 4);
    c.scalar(v.win32_error, 4);
    c.scalar(v.minhook_status, 4);
    c.scalar(v.byte_count, 4);
    c.scalar(v.collision_rva, 4);
    c.scalar(v.byte_window_offset, 4);
    for (size_t i = 0; i < 32; ++i) c.scalar(v.expected_bytes[i], 1);
    for (size_t i = 0; i < 32; ++i) c.scalar(v.actual_bytes[i], 1);
}
template<class C, class T> void v1_transfer_context_result(C& c, T& v) {
    c.scalar(v.abi_version, 4);
    c.scalar(v.sequence, 8);
    c.scalar(v.sampled_at_ms, 8);
    c.scalar(v.duration_ms, 4);
    c.scalar(v.sample_reason, 4);
    c.scalar(v.profile, 4);
    c.scalar(v.locator_revision, 4);
    c.scalar(v.layout_revision, 4);
    c.scalar(v.root_locator_reason, 4);
    c.scalar(v.disk_hash_reason, 4);
    for (size_t i = 0; i < 64; ++i) c.scalar(v.disk_sha256[i], 1);
}
template<class C, class T> void v1_transfer_context_map(C& c, T& v) {
    c.scalar(v.validity, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.win32_error, 4);
    c.scalar(v.length, 2);
}
template<class C, class T> void v1_transfer_native_map(C& c, T& v) {
    c.scalar(v.validity, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.win32_error, 4);
    c.scalar(v.length, 4);
}
template<class C, class T> void v1_transfer_native_counts(C& c, T& v) {
    c.scalar(v.queued, 4);
    c.scalar(v.claimed, 4);
    c.scalar(v.retained_results, 4);
    c.scalar(v.history_gap, 4);
    c.scalar(v.event_count, 4);
}
template<class C, class T> void v1_transfer_native_event(C& c, T& v) {
    c.scalar(v.sequence, 8);
    c.scalar(v.generation, 8);
    c.scalar(v.at_ms, 8);
    c.scalar(v.kind, 4);
    c.scalar(v.lifecycle, 4);
    c.scalar(v.thread_id, 4);
    c.scalar(v.depth, 4);
}
template<class C, class T> void v1_transfer_native_result(C& c, T& v) {
    c.scalar(v.scope.pid, 4);
    c.scalar(v.scope.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.scope.instance_id[i], 1);
    c.scalar(v.scope.lifecycle_generation, 8);
    c.scalar(v.availability, 4);
    c.scalar(v.reason, 4);
    c.scalar(v.site_revision, 4);
    c.scalar(v.site_rva, 4);
    c.scalar(v.phase, 4);
    c.scalar(v.installed_hooks, 4);
    for (size_t i = 0; i < 3; ++i) c.scalar(v.validator_reasons[i], 4);
    c.scalar(v.retained_module, 4);
    c.scalar(v.coverage, 4);
    c.scalar(v.lifecycle, 4);
    c.scalar(v.depth, 4);
    c.scalar(v.checkpoint_flag_known, 4);
    c.scalar(v.checkpoint_flag, 4);
    c.scalar(v.event_sequence, 8);
    c.scalar(v.event_gap_count, 8);
    c.scalar(v.history_oldest, 8);
    c.scalar(v.history_overwritten, 8);
    c.scalar(v.callback_sequence, 8);
    c.scalar(v.callback_at_ms, 8);
    c.scalar(v.callback_thread_id, 4);
    c.scalar(v.native_owner_thread_id, 4);
    c.scalar(v.context_generation, 8);
    c.scalar(v.context_sampled_at_ms, 8);
    c.scalar(v.context_reason, 4);
    c.scalar(v.game_state, 4);
}
template<class C, class T> void v1_transfer_header(C& c, T& v) {
    c.scalar(v.magic, 4);
    c.scalar(v.version, 2);
    c.scalar(v.operation, 2);
    c.scalar(v.payload_length, 4);
    c.scalar(v.result, 4);
}
template<class C, class T> void v1_transfer_inspect_core(C& c, T& v) {
    c.scalar(v.core.abi_version, 4);
    c.scalar(v.core.capabilities, 8);
}
template<class C, class T> void v1_transfer_process_identity(C& c, T& v) {
    c.scalar(v.pid, 4);
    c.scalar(v.process_created, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.instance[i], 1);
}
template<class C, class T> void v1_transfer_inspect_state(C& c, T& v) {
    c.scalar(v.core.state, 4);
    c.scalar(v.service, 4);
    c.scalar(v.service_error, 4);
}
template<class C, class T> void v1_transfer_inspect_tail(C& c, T& v) {
    c.scalar(v.core.last_result, 4);
    c.scalar(v.core.initialization_count, 4);
    c.text(v.core.version);
    c.text(v.core.build_id);
}
template<class C, class T> void v1_transfer_combat_query(C& c, T& v) {
    c.text(v.namespace_id);
    c.scalar(v.map_count, 4);
}
template<class C, class T> void v1_transfer_campaign_rewards_header(C& c, T& v) {
    c.scalar(v.known, 4);
    c.scalar(v.count, 4);
}
template<class C, class T> void v1_transfer_campaign_intent_count(C& c, T& v) {
    c.scalar(v.intent_count, 4);
}
template<class C, class T> void v1_transfer_map_bytes(C& c, T& v) {
    if (v.length > 255) { c.valid = false; return; }
    for (size_t i = 0; i < v.length; ++i) c.scalar(v.bytes[i], 1);
}
template<class C, class T> void v1_transfer_combat_maps(C& c, T& v) {
    if (v.map_count > 64) { c.valid = false; return; }
    for (size_t i = 0; i < v.map_count; ++i) c.text(v.maps[i]);
}
template<class C, class T> void v1_transfer_campaign_intents(C& c, T& v) {
    if (v.intent_count > 96) { c.valid = false; return; }
    for (size_t i = 0; i < v.intent_count; ++i) c.scalar(v.intent_ids[i], 4);
}
enum class V1Family { empty, cursor, diagnostic, write, backup, points, campaign, inventory, arsenal, runes, special, deathlink, automap, command, combat, presentation };
struct V1Descriptor { uint16_t version, opcode; uint64_t capability; size_t payload; V1Family family; bool subset, envelope, allow_zero_generation; uint32_t input_abi; };
inline constexpr V1Descriptor v1_descriptors[] = {
    {1, 1, 1ULL, 8, V1Family::empty, true, false, false, 0},
    {1, 2, 2ULL, 8, V1Family::empty, true, false, false, 0},
    {1, 3, 4ULL, 8, V1Family::empty, true, false, false, 0},
    {1, 4, 8ULL, 16, V1Family::cursor, false, false, false, 0},
    {1, 5, 16ULL, 72, V1Family::diagnostic, false, true, false, 0},
    {1, 6, 16ULL, 72, V1Family::diagnostic, false, true, false, 0},
    {1, 7, 16ULL, 72, V1Family::diagnostic, false, true, false, 0},
    {1, 8, 32ULL, 72, V1Family::diagnostic, false, true, false, 0},
    {1, 9, 32ULL, 72, V1Family::diagnostic, false, true, false, 0},
    {1, 10, 32ULL, 72, V1Family::diagnostic, false, true, false, 0},
    {1, 11, 64ULL, 8, V1Family::empty, true, false, false, 0},
    {1, 12, 128ULL, 8, V1Family::empty, true, false, false, 0},
    {1, 13, 256ULL, 16, V1Family::write, false, false, false, 0},
    {1, 14, 512ULL, 149, V1Family::backup, false, true, false, 0},
    {1, 15, 512ULL, 149, V1Family::backup, false, true, false, 0},
    {1, 16, 512ULL, 149, V1Family::backup, false, true, false, 0},
    {1, 17, 1024ULL, 8, V1Family::empty, false, false, false, 0},
    {1, 18, 2048ULL, 149, V1Family::points, false, true, false, 0},
    {1, 19, 2048ULL, 149, V1Family::points, false, true, false, 0},
    {1, 20, 2048ULL, 149, V1Family::points, false, true, false, 0},
    {1, 21, 2048ULL, 149, V1Family::points, false, true, false, 0},
    {1, 22, 4096ULL, 453, V1Family::campaign, false, true, true, 0},
    {1, 23, 4096ULL, 453, V1Family::campaign, false, true, true, 0},
    {1, 24, 4096ULL, 453, V1Family::campaign, false, true, true, 0},
    {1, 25, 8192ULL, 161, V1Family::inventory, false, true, false, 0},
    {1, 26, 8192ULL, 161, V1Family::inventory, false, true, false, 0},
    {1, 27, 8192ULL, 161, V1Family::inventory, false, true, false, 0},
    {1, 28, 8192ULL, 161, V1Family::inventory, false, true, false, 0},
    {1, 29, 16384ULL, 165, V1Family::arsenal, false, true, false, 0},
    {1, 30, 16384ULL, 165, V1Family::arsenal, false, true, false, 0},
    {1, 31, 16384ULL, 165, V1Family::arsenal, false, true, false, 0},
    {1, 32, 16384ULL, 165, V1Family::arsenal, false, true, false, 0},
    {1, 33, 32768ULL, 153, V1Family::runes, false, true, false, 0},
    {1, 34, 32768ULL, 153, V1Family::runes, false, true, false, 0},
    {1, 35, 32768ULL, 153, V1Family::runes, false, true, false, 0},
    {1, 36, 32768ULL, 153, V1Family::runes, false, true, false, 0},
    {1, 37, 65536ULL, 181, V1Family::special, false, true, false, 0},
    {1, 38, 65536ULL, 181, V1Family::special, false, true, false, 0},
    {1, 39, 65536ULL, 181, V1Family::special, false, true, false, 0},
    {1, 40, 65536ULL, 181, V1Family::special, false, true, false, 0},
    {1, 41, 131072ULL, 189, V1Family::deathlink, false, true, false, 0},
    {1, 42, 131072ULL, 189, V1Family::deathlink, false, true, false, 0},
    {1, 43, 131072ULL, 189, V1Family::deathlink, false, true, false, 0},
    {1, 44, 131072ULL, 189, V1Family::deathlink, false, true, false, 0},
    {1, 45, 262144ULL, 225, V1Family::automap, false, true, false, 0},
    {1, 46, 2097152ULL, 2193, V1Family::command, false, true, false, 1},
    {1, 47, 2097152ULL, 2193, V1Family::command, false, true, false, 1},
    {1, 48, 2097152ULL, 2193, V1Family::command, false, true, false, 1},
    {1, 49, 2097152ULL, 2193, V1Family::command, false, true, false, 1},
    {1, 50, 4194304ULL, 0, V1Family::combat, false, false, false, 2},
    {1, 51, 8388608ULL, 185, V1Family::presentation, false, true, false, 1},
};
inline sc_status to_wire(const ::sc_status& v) {
    sc_status out{};
    out.abi_version = v.abi_version;
    out.capabilities = v.capabilities;
    out.state = v.state;
    out.last_result = v.last_result;
    out.initialization_count = v.initialization_count;
    out.diagnostic_count = v.diagnostic_count;
    for (size_t i = 0; i < 32; ++i) out.version[i] = v.version[i];
    for (size_t i = 0; i < 65; ++i) out.build_id[i] = v.build_id[i];
    return out;
}
inline sc_engine_field to_wire(const ::sc_engine_field& v) {
    sc_engine_field out{};
    out.validity = v.validity;
    out.reason = v.reason;
    out.value = v.value;
    out.win32_error = v.win32_error;
    return out;
}
inline sc_engine_snapshot to_wire(const ::sc_engine_snapshot& v) {
    sc_engine_snapshot out{};
    out.abi_version = v.abi_version;
    out.sequence = v.sequence;
    out.sampled_at_ms = v.sampled_at_ms;
    out.duration_ms = v.duration_ms;
    out.sample_reason = v.sample_reason;
    out.pe_reason = v.pe_reason;
    out.machine = v.machine;
    out.timestamp = v.timestamp;
    out.image_size = v.image_size;
    out.entry_rva = v.entry_rva;
    out.disk_hash_reason = v.disk_hash_reason;
    for (size_t i = 0; i < 65; ++i) out.disk_sha256[i] = v.disk_sha256[i];
    out.profile = v.profile;
    out.locator_revision = v.locator_revision;
    out.root_locator_reason = v.root_locator_reason;
    out.root_signature_rva = v.root_signature_rva;
    out.root_target_rva = v.root_target_rva;
    out.profile_runtime_validated = v.profile_runtime_validated;
    for (size_t i = 0; i < 6; ++i) out.fields[i] = to_wire(v.fields[i]);
    return out;
}
inline sc_context_map to_wire(const ::sc_context_map& v) {
    sc_context_map out{};
    out.validity = v.validity;
    out.reason = v.reason;
    out.win32_error = v.win32_error;
    out.length = v.length;
    for (size_t i = 0; i < 256; ++i) out.bytes[i] = v.bytes[i];
    return out;
}
inline sc_context_field to_wire(const ::sc_context_field& v) {
    sc_context_field out{};
    out.validity = v.validity;
    out.reason = v.reason;
    out.value = v.value;
    out.win32_error = v.win32_error;
    return out;
}
inline sc_context_snapshot to_wire(const ::sc_context_snapshot& v) {
    sc_context_snapshot out{};
    out.abi_version = v.abi_version;
    out.pid = v.pid;
    out.process_created = v.process_created;
    for (size_t i = 0; i < 16; ++i) out.instance[i] = v.instance[i];
    out.sequence = v.sequence;
    out.sampled_at_ms = v.sampled_at_ms;
    out.duration_ms = v.duration_ms;
    out.sample_reason = v.sample_reason;
    out.profile = v.profile;
    out.locator_revision = v.locator_revision;
    out.layout_revision = v.layout_revision;
    out.root_locator_reason = v.root_locator_reason;
    out.disk_hash_reason = v.disk_hash_reason;
    for (size_t i = 0; i < 65; ++i) out.disk_sha256[i] = v.disk_sha256[i];
    out.current_map = to_wire(v.current_map);
    for (size_t i = 0; i < 5; ++i) out.fields[i] = to_wire(v.fields[i]);
    return out;
}
inline sc_native_scope to_wire(const ::sc_native_scope& v) {
    sc_native_scope out{};
    out.pid = v.pid;
    out.process_created = v.process_created;
    for (size_t i = 0; i < 16; ++i) out.instance_id[i] = v.instance_id[i];
    out.lifecycle_generation = v.lifecycle_generation;
    return out;
}
inline ::sc_native_scope from_wire(const sc_native_scope& v) {
    ::sc_native_scope out{};
    out.pid = v.pid;
    out.process_created = v.process_created;
    for (size_t i = 0; i < 16; ++i) out.instance_id[i] = v.instance_id[i];
    out.lifecycle_generation = v.lifecycle_generation;
    return out;
}
inline sc_native_event to_wire(const ::sc_native_event& v) {
    sc_native_event out{};
    out.sequence = v.sequence;
    out.generation = v.generation;
    out.at_ms = v.at_ms;
    out.kind = v.kind;
    out.lifecycle = v.lifecycle;
    out.thread_id = v.thread_id;
    out.depth = v.depth;
    return out;
}
inline sc_native_snapshot to_wire(const ::sc_native_snapshot& v) {
    sc_native_snapshot out{};
    out.abi_version = v.abi_version;
    out.scope = to_wire(v.scope);
    out.availability = v.availability;
    out.reason = v.reason;
    out.site_revision = v.site_revision;
    out.site_rva = v.site_rva;
    out.phase = v.phase;
    out.installed_hooks = v.installed_hooks;
    for (size_t i = 0; i < 3; ++i) out.validator_reasons[i] = v.validator_reasons[i];
    out.retained_module = v.retained_module;
    out.coverage = v.coverage;
    out.lifecycle = v.lifecycle;
    out.depth = v.depth;
    out.checkpoint_flag_known = v.checkpoint_flag_known;
    out.checkpoint_flag = v.checkpoint_flag;
    out.event_sequence = v.event_sequence;
    out.event_gap_count = v.event_gap_count;
    out.history_oldest = v.history_oldest;
    out.history_overwritten = v.history_overwritten;
    out.callback_sequence = v.callback_sequence;
    out.callback_at_ms = v.callback_at_ms;
    out.callback_thread_id = v.callback_thread_id;
    out.native_owner_thread_id = v.native_owner_thread_id;
    out.context_generation = v.context_generation;
    out.context_sampled_at_ms = v.context_sampled_at_ms;
    out.context_reason = v.context_reason;
    out.game_state = v.game_state;
    out.current_map = to_wire(v.current_map);
    out.queued = v.queued;
    out.claimed = v.claimed;
    out.retained_results = v.retained_results;
    out.history_gap = v.history_gap;
    out.event_count = v.event_count;
    for (size_t i = 0; i < 6; ++i) out.events[i] = to_wire(v.events[i]);
    return out;
}
inline sc_diagnostic_result to_wire(const ::sc_diagnostic_result& v) {
    sc_diagnostic_result out{};
    out.state = v.state;
    out.reason = v.reason;
    out.cancel_requested = v.cancel_requested;
    out.retrieved = v.retrieved;
    out.scope = to_wire(v.scope);
    out.request_id = v.request_id;
    for (size_t i = 0; i < 16; ++i) out.nonce[i] = v.nonce[i];
    out.admitted_at_ms = v.admitted_at_ms;
    out.deadline_at_ms = v.deadline_at_ms;
    out.claimed_at_ms = v.claimed_at_ms;
    out.observed_at_ms = v.observed_at_ms;
    out.executed_at_ms = v.executed_at_ms;
    out.completed_at_ms = v.completed_at_ms;
    out.retrieved_at_ms = v.retrieved_at_ms;
    out.thread_id = v.thread_id;
    out.site_revision = v.site_revision;
    out.phase = v.phase;
    out.lifecycle = v.lifecycle;
    out.game_state = v.game_state;
    out.current_map = to_wire(v.current_map);
    return out;
}
inline sc_save_field to_wire(const ::sc_save_field& v) {
    sc_save_field out{};
    out.validity = v.validity;
    out.reason = v.reason;
    out.value = v.value;
    out.win32_error = v.win32_error;
    out.reserved = v.reserved;
    return out;
}
inline sc_save_snapshot to_wire(const ::sc_save_snapshot& v) {
    sc_save_snapshot out{};
    out.abi_version = v.abi_version;
    out.pid = v.pid;
    out.profile = v.profile;
    out.process_created = v.process_created;
    for (size_t i = 0; i < 16; ++i) out.instance[i] = v.instance[i];
    out.sequence = v.sequence;
    out.sampled_at_ms = v.sampled_at_ms;
    out.duration_ms = v.duration_ms;
    out.sample_reason = v.sample_reason;
    out.layout_revision = v.layout_revision;
    out.root_locator_reason = v.root_locator_reason;
    out.mutation_available = v.mutation_available;
    out.mutation_reason = v.mutation_reason;
    for (size_t i = 0; i < 12; ++i) out.fields[i] = to_wire(v.fields[i]);
    return out;
}
inline sc_save_admission_snapshot to_wire(const ::sc_save_admission_snapshot& v) {
    sc_save_admission_snapshot out{};
    out.abi_version = v.abi_version;
    out.state = v.state;
    out.fault = v.fault;
    out.prepared_routes = v.prepared_routes;
    out.required_routes = v.required_routes;
    out.flags = v.flags;
    out.reserved = v.reserved;
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    for (size_t i = 0; i < 44; ++i) out.native_root[i] = v.native_root[i];
    for (size_t i = 0; i < 19; ++i) out.reserved_bytes[i] = v.reserved_bytes[i];
    return out;
}
inline sc_save_write_snapshot to_wire(const ::sc_save_write_snapshot& v) {
    sc_save_write_snapshot out{};
    out.abi_version = v.abi_version;
    out.state = v.state;
    out.flags = v.flags;
    out.operation_id = v.operation_id;
    out.sdk_sequence = v.sdk_sequence;
    out.file_count = v.file_count;
    out.submitted = v.submitted;
    out.completed = v.completed;
    out.pending_handles = v.pending_handles;
    out.preparation_jobs = v.preparation_jobs;
    out.preflight_jobs = v.preflight_jobs;
    out.native_value = v.native_value;
    out.reserved = v.reserved;
    out.native_state = v.native_state;
    out.native_outcome = v.native_outcome;
    for (size_t i = 0; i < 64; ++i) out.directory[i] = v.directory[i];
    for (size_t i = 0; i < 16; ++i) out.reserved_bytes[i] = v.reserved_bytes[i];
    return out;
}
inline sc_save_backup_snapshot to_wire(const ::sc_save_backup_snapshot& v) {
    sc_save_backup_snapshot out{};
    out.abi_version = v.abi_version;
    out.state = v.state;
    out.failure = v.failure;
    out.execution = to_wire(v.execution);
    out.operation_id = v.operation_id;
    out.flags = v.flags;
    out.native_exception = v.native_exception;
    out.storage_outcome = v.storage_outcome;
    out.storage_error = v.storage_error;
    out.files = v.files;
    out.bytes = v.bytes;
    for (size_t i = 0; i < 80; ++i) out.basename[i] = v.basename[i];
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.campaign = v.campaign;
    out.slot = v.slot;
    return out;
}
inline sc_install_event to_wire(const ::sc_install_event& v) {
    sc_install_event out{};
    out.sequence = v.sequence;
    out.at_ms = v.at_ms;
    out.duration_ms = v.duration_ms;
    out.stage = v.stage;
    out.target_group = v.target_group;
    out.target_index = v.target_index;
    out.rva = v.rva;
    out.signature_offset = v.signature_offset;
    out.result = v.result;
    out.reason = v.reason;
    out.read_reason = v.read_reason;
    out.win32_error = v.win32_error;
    out.minhook_status = v.minhook_status;
    out.byte_count = v.byte_count;
    out.collision_rva = v.collision_rva;
    out.byte_window_offset = v.byte_window_offset;
    for (size_t i = 0; i < 32; ++i) out.expected_bytes[i] = v.expected_bytes[i];
    for (size_t i = 0; i < 32; ++i) out.actual_bytes[i] = v.actual_bytes[i];
    return out;
}
inline sc_save_installation_snapshot to_wire(const ::sc_save_installation_snapshot& v) {
    sc_save_installation_snapshot out{};
    out.abi_version = v.abi_version;
    out.attempt = v.attempt;
    out.phase = v.phase;
    out.last_completed_stage = v.last_completed_stage;
    out.startup_observation = v.startup_observation;
    out.validated = v.validated;
    out.created = v.created;
    out.enabled = v.enabled;
    out.cleanup_failures = v.cleanup_failures;
    out.gaps = v.gaps;
    out.reserved = v.reserved;
    out.sequence = v.sequence;
    out.active = to_wire(v.active);
    out.primary_failure = to_wire(v.primary_failure);
    out.cleanup_failure = to_wire(v.cleanup_failure);
    return out;
}
inline sc_weapon_points_result to_wire(const ::sc_weapon_points_result& v) {
    sc_weapon_points_result out{};
    out.abi_version = v.abi_version;
    out.execution = to_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.kind = v.kind;
    out.amount = v.amount;
    out.expected_gained = v.expected_gained;
    out.outcome = v.outcome;
    out.flags = v.flags;
    out.native_exception = v.native_exception;
    out.balance_before = v.balance_before;
    out.balance_after = v.balance_after;
    out.gained_before = v.gained_before;
    out.gained_after = v.gained_after;
    out.suppressed_grants = v.suppressed_grants;
    return out;
}
inline sc_campaign_result to_wire(const ::sc_campaign_result& v) {
    sc_campaign_result out{};
    out.abi_version = v.abi_version;
    out.scope = to_wire(v.scope);
    out.request_id = v.request_id;
    for (size_t i = 0; i < 16; ++i) out.nonce[i] = v.nonce[i];
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.status = v.status;
    out.reason = v.reason;
    out.committed_revision = v.committed_revision;
    out.rendered_revision = v.rendered_revision;
    out.selected_id = v.selected_id;
    out.loaded_id = v.loaded_id;
    out.intent_count = v.intent_count;
    for (size_t i = 0; i < 96; ++i) out.intent_ids[i] = v.intent_ids[i];
    return out;
}
inline sc_inventory_result to_wire(const ::sc_inventory_result& v) {
    sc_inventory_result out{};
    out.abi_version = v.abi_version;
    out.execution = to_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.kind = v.kind;
    out.outcome = v.outcome;
    out.flags = v.flags;
    out.native_exception = v.native_exception;
    out.weapons_before = v.weapons_before;
    out.weapons_after = v.weapons_after;
    out.equipment_before = v.equipment_before;
    out.equipment_after = v.equipment_after;
    out.special_before = v.special_before;
    out.special_after = v.special_after;
    out.upgrades_before = v.upgrades_before;
    out.upgrades_after = v.upgrades_after;
    out.health_tier_before = v.health_tier_before;
    out.health_tier_after = v.health_tier_after;
    out.armor_tier_before = v.armor_tier_before;
    out.armor_tier_after = v.armor_tier_after;
    out.ammo_tier_before = v.ammo_tier_before;
    out.ammo_tier_after = v.ammo_tier_after;
    out.reserved_before = v.reserved_before;
    out.reserved_after = v.reserved_after;
    out.operations_applied = v.operations_applied;
    out.ice_bomb_before = v.ice_bomb_before;
    out.ice_bomb_after = v.ice_bomb_after;
    out.dash_before = v.dash_before;
    out.dash_after = v.dash_after;
    out.slayer_key_before = v.slayer_key_before;
    out.slayer_key_after = v.slayer_key_after;
    out.frag_grenade_before = v.frag_grenade_before;
    out.frag_grenade_after = v.frag_grenade_after;
    out.flame_belch_before = v.flame_belch_before;
    out.flame_belch_after = v.flame_belch_after;
    out.blood_punch_before = v.blood_punch_before;
    out.blood_punch_after = v.blood_punch_after;
    out.perks_known = v.perks_known;
    out.perks_owned = v.perks_owned;
    return out;
}
inline sc_arsenal_result to_wire(const ::sc_arsenal_result& v) {
    sc_arsenal_result out{};
    out.abi_version = v.abi_version;
    out.execution = to_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.kind = v.kind;
    out.outcome = v.outcome;
    out.flags = v.flags;
    out.native_exception = v.native_exception;
    out.weapons_before = v.weapons_before;
    out.weapons_after = v.weapons_after;
    out.mods_before = v.mods_before;
    out.mods_after = v.mods_after;
    for (size_t i = 0; i < 8; ++i) out.selected_mods_before[i] = v.selected_mods_before[i];
    for (size_t i = 0; i < 8; ++i) out.selected_mods_after[i] = v.selected_mods_after[i];
    out.normal_upgrades_before = v.normal_upgrades_before;
    out.normal_upgrades_after = v.normal_upgrades_after;
    out.masteries_ap_before = v.masteries_ap_before;
    out.masteries_ap_after = v.masteries_ap_after;
    out.mastery_challenges_active_before = v.mastery_challenges_active_before;
    out.mastery_challenges_active_after = v.mastery_challenges_active_after;
    out.mastery_challenges_completed_before = v.mastery_challenges_completed_before;
    out.mastery_challenges_completed_after = v.mastery_challenges_completed_after;
    out.masteries_effective_before = v.masteries_effective_before;
    out.masteries_effective_after = v.masteries_effective_after;
    out.mission_challenges_active_before = v.mission_challenges_active_before;
    out.mission_challenges_active_after = v.mission_challenges_active_after;
    out.mission_challenges_completed_before = v.mission_challenges_completed_before;
    out.mission_challenges_completed_after = v.mission_challenges_completed_after;
    out.operations_applied = v.operations_applied;
    return out;
}
inline sc_runes_result to_wire(const ::sc_runes_result& v) {
    sc_runes_result out{};
    out.abi_version = v.abi_version;
    out.execution = to_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.kind = v.kind;
    out.outcome = v.outcome;
    out.flags = v.flags;
    out.native_exception = v.native_exception;
    out.owned_normal_before = v.owned_normal_before;
    out.owned_normal_after = v.owned_normal_after;
    out.owned_support_before = v.owned_support_before;
    out.owned_support_after = v.owned_support_after;
    for (size_t i = 0; i < 3; ++i) out.selected_slots_before[i] = v.selected_slots_before[i];
    for (size_t i = 0; i < 3; ++i) out.selected_slots_after[i] = v.selected_slots_after[i];
    out.selected_support_before = v.selected_support_before;
    out.selected_support_after = v.selected_support_after;
    out.unlocked_slots_before = v.unlocked_slots_before;
    out.unlocked_slots_after = v.unlocked_slots_after;
    out.derived_pairs_before = v.derived_pairs_before;
    out.derived_pairs_after = v.derived_pairs_after;
    out.reserved_before = v.reserved_before;
    out.reserved_after = v.reserved_after;
    out.operations_applied = v.operations_applied;
    return out;
}
inline sc_special_result to_wire(const ::sc_special_result& v) {
    sc_special_result out{};
    out.abi_version = v.abi_version;
    out.execution = to_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.kind = v.kind;
    out.outcome = v.outcome;
    out.flags = v.flags;
    out.native_exception = v.native_exception;
    out.owns_crucible = v.owns_crucible;
    out.owns_hammer = v.owns_hammer;
    out.hammer_tier = v.hammer_tier;
    out.selected = v.selected;
    out.native_crucible = v.native_crucible;
    out.native_hammer = v.native_hammer;
    out.native_hammer_perks = v.native_hammer_perks;
    out.native_selected = v.native_selected;
    out.native_state_known = v.native_state_known;
    out.crucible_charge = v.crucible_charge;
    out.crucible_charge_max = v.crucible_charge_max;
    out.refill_balance = v.refill_balance;
    out.refill_flags = v.refill_flags;
    out.refill_request_id = v.refill_request_id;
    out.refill_request_state = v.refill_request_state;
    out.refill_executed = v.refill_executed;
    out.refill_execution_id = v.refill_execution_id;
    out.refill_sequence = v.refill_sequence;
    out.reserved0 = v.reserved0;
    out.operations_applied = v.operations_applied;
    return out;
}
inline sc_deathlink_result to_wire(const ::sc_deathlink_result& v) {
    sc_deathlink_result out{};
    out.abi_version = v.abi_version;
    out.execution = to_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.kind = v.kind;
    out.outcome = v.outcome;
    out.flags = v.flags;
    out.native_exception = v.native_exception;
    out.enabled = v.enabled;
    out.mode = v.mode;
    out.remote_state = v.remote_state;
    out.remote_protection = v.remote_protection;
    out.remote_attempts = v.remote_attempts;
    out.pending_count = v.pending_count;
    out.remote_event_id = v.remote_event_id;
    out.local_candidates = v.local_candidates;
    out.local_suppressed = v.local_suppressed;
    out.local_state = v.local_state;
    out.local_death_sequence = v.local_death_sequence;
    out.local_death_time_ms = v.local_death_time_ms;
    out.local_cause = v.local_cause;
    out.local_protection = v.local_protection;
    out.local_flags = v.local_flags;
    out.reserved0 = v.reserved0;
    out.operations_applied = v.operations_applied;
    return out;
}
inline sc_automap_result to_wire(const ::sc_automap_result& v) {
    sc_automap_result out{};
    out.abi_version = v.abi_version;
    out.scope = to_wire(v.scope);
    out.request_id = v.request_id;
    for (size_t i = 0; i < 16; ++i) out.nonce[i] = v.nonce[i];
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.kind = v.kind;
    out.outcome = v.outcome;
    out.known = v.known;
    out.native_fault = v.native_fault;
    out.revision = v.revision;
    out.scanned = v.scanned;
    out.collected = v.collected;
    out.completed_passes = v.completed_passes;
    return out;
}
inline sc_command_result to_wire(const ::sc_command_result& v) {
    sc_command_result out{};
    out.abi_version = v.abi_version;
    out.execution = to_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.kind = v.kind;
    out.outcome = v.outcome;
    out.native_exception = v.native_exception;
    return out;
}
inline sc_combat_snapshot to_wire(const ::sc_combat_snapshot& v) {
    sc_combat_snapshot out{};
    out.abi_version = v.abi_version;
    out.scope = to_wire(v.scope);
    out.availability = v.availability;
    out.reason = v.reason;
    out.observation_gaps = v.observation_gaps;
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.qualified_kills = v.qualified_kills;
    return out;
}
inline sc_combat_presentation_result to_wire(const ::sc_combat_presentation_result& v) {
    sc_combat_presentation_result out{};
    out.abi_version = v.abi_version;
    out.scope = to_wire(v.scope);
    out.request_id = v.request_id;
    for (size_t i = 0; i < 16; ++i) out.nonce[i] = v.nonce[i];
    out.status = v.status;
    out.reason = v.reason;
    out.total = v.total;
    out.target = v.target;
    out.milestone = v.milestone;
    out.visible = v.visible;
    return out;
}
inline sc_campaign_row to_wire(const ::sc_campaign_row& v) {
    sc_campaign_row out{};
    out.id = v.id;
    out.flags = v.flags;
    out.native_index = v.native_index;
    for (size_t i = 0; i < 192; ++i) out.map[i] = v.map[i];
    for (size_t i = 0; i < 96; ++i) out.title[i] = v.title[i];
    return out;
}
inline ::sc_campaign_row from_wire(const sc_campaign_row& v) {
    ::sc_campaign_row out{};
    out.id = v.id;
    out.flags = v.flags;
    out.native_index = v.native_index;
    for (size_t i = 0; i < 192; ++i) out.map[i] = v.map[i];
    for (size_t i = 0; i < 96; ++i) out.title[i] = v.title[i];
    return out;
}
inline sc_physical_challenge to_wire(const ::sc_physical_challenge& v) {
    sc_physical_challenge out{};
    for (size_t i = 0; i < 80; ++i) out.unlockable[i] = v.unlockable[i];
    out.found = v.found;
    out.required = v.required;
    out.checked = v.checked;
    return out;
}
inline ::sc_physical_challenge from_wire(const sc_physical_challenge& v) {
    ::sc_physical_challenge out{};
    for (size_t i = 0; i < 80; ++i) out.unlockable[i] = v.unlockable[i];
    out.found = v.found;
    out.required = v.required;
    out.checked = v.checked;
    return out;
}
inline sc_campaign_summary to_wire(const ::sc_campaign_summary& v) {
    sc_campaign_summary out{};
    out.known = v.known;
    out.found = v.found;
    out.total = v.total;
    out.band = v.band;
    for (size_t i = 0; i < 3; ++i) out.challenges[i] = to_wire(v.challenges[i]);
    return out;
}
inline ::sc_campaign_summary from_wire(const sc_campaign_summary& v) {
    ::sc_campaign_summary out{};
    out.known = v.known;
    out.found = v.found;
    out.total = v.total;
    out.band = v.band;
    for (size_t i = 0; i < 3; ++i) out.challenges[i] = from_wire(v.challenges[i]);
    return out;
}
inline sc_diagnostic_request to_wire(const ::sc_diagnostic_request& v) {
    sc_diagnostic_request out{};
    out.expected = to_wire(v.expected);
    out.request_id = v.request_id;
    for (size_t i = 0; i < 16; ++i) out.nonce[i] = v.nonce[i];
    out.deadline_ms = v.deadline_ms;
    return out;
}
inline ::sc_diagnostic_request from_wire(const sc_diagnostic_request& v) {
    ::sc_diagnostic_request out{};
    out.expected = from_wire(v.expected);
    out.request_id = v.request_id;
    for (size_t i = 0; i < 16; ++i) out.nonce[i] = v.nonce[i];
    out.deadline_ms = v.deadline_ms;
    return out;
}
inline sc_combat_presentation_request to_wire(const ::sc_combat_presentation_request& v) {
    sc_combat_presentation_request out{};
    out.execution = to_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.total = v.total;
    out.target = v.target;
    out.first_milestone = v.first_milestone;
    out.milestone = v.milestone;
    out.milestone_step = v.milestone_step;
    out.visible = v.visible;
    return out;
}
inline ::sc_combat_presentation_request from_wire(const sc_combat_presentation_request& v) {
    ::sc_combat_presentation_request out{};
    out.execution = from_wire(v.execution);
    for (size_t i = 0; i < 65; ++i) out.namespace_id[i] = v.namespace_id[i];
    out.total = v.total;
    out.target = v.target;
    out.first_milestone = v.first_milestone;
    out.milestone = v.milestone;
    out.milestone_step = v.milestone_step;
    out.visible = v.visible;
    return out;
}
inline sc_diagnostic_detail to_wire(const ::sc_diagnostic_detail& v) {
    sc_diagnostic_detail out{};
    out.revision = v.revision;
    out.stage = v.stage;
    out.observation_attempted = v.observation_attempted;
    out.observation_accepted = v.observation_accepted;
    out.timing_valid = v.timing_valid;
    out.timing_error = v.timing_error;
    out.claim_lock_missed = v.claim_lock_missed;
    out.observation_started_at_ms = v.observation_started_at_ms;
    out.observation_elapsed_ns = v.observation_elapsed_ns;
    out.observation_budget_ns = v.observation_budget_ns;
    out.sample_reason = v.sample_reason;
    out.state_validity = v.state_validity;
    out.state_reason = v.state_reason;
    out.state_error = v.state_error;
    out.map_validity = v.map_validity;
    out.map_reason = v.map_reason;
    out.map_error = v.map_error;
    out.pending_before = v.pending_before;
    out.pending_before_reason = v.pending_before_reason;
    out.pending_before_error = v.pending_before_error;
    out.pending_after = v.pending_after;
    out.pending_after_reason = v.pending_after_reason;
    out.pending_after_error = v.pending_after_error;
    return out;
}
struct NativeRead { uint64_t cursor = 0; };
struct NativeCampaign { ::sc_campaign_request request{}; ::sc_campaign_summary summary{}; ::sc_campaign_rewards rewards{}; };
using NativeRequest = std::variant<std::monostate, NativeRead, ::sc_diagnostic_request, ::sc_save_backup_request, ::sc_weapon_points_request, NativeCampaign, ::sc_inventory_request, ::sc_arsenal_request, ::sc_runes_request, ::sc_special_request, ::sc_deathlink_request, ::sc_automap_request, ::sc_command_request, ::sc_combat_query, ::sc_combat_presentation_request>;
using NativeResult = std::variant<std::monostate, ::sc_status, ::sc_engine_snapshot, ::sc_context_snapshot, ::sc_native_snapshot, ::sc_diagnostic_result, ::sc_save_snapshot, ::sc_save_admission_snapshot, ::sc_save_write_snapshot, ::sc_save_backup_snapshot, ::sc_save_installation_snapshot, ::sc_weapon_points_result, ::sc_campaign_result, ::sc_inventory_result, ::sc_arsenal_result, ::sc_runes_result, ::sc_special_result, ::sc_deathlink_result, ::sc_automap_result, ::sc_command_result, ::sc_combat_snapshot, ::sc_combat_presentation_result>;
template<class T> struct NativeBinding;
template<> struct NativeBinding<NativeRead> { using Result = ::sc_status; static constexpr CommandId id = CommandId::inspect; };
template<> struct NativeBinding<::sc_diagnostic_request> { using Result = ::sc_diagnostic_result; static constexpr CommandId id = CommandId::diagnostic; };
template<> struct NativeBinding<::sc_save_backup_request> { using Result = ::sc_save_backup_snapshot; static constexpr CommandId id = CommandId::save_backup; };
template<class C> void native_transfer(C& c, const ::sc_save_backup_request& v) {
    c.scalar(v.campaign, 4);
    c.scalar(v.slot, 4);
    c.scalar(v.work_deadline_ms, 4);
}
inline const char* native_namespace(const ::sc_save_backup_request& v) { return v.namespace_id; }
template<> struct NativeBinding<::sc_weapon_points_request> { using Result = ::sc_weapon_points_result; static constexpr CommandId id = CommandId::weapon_points; };
template<class C> void native_transfer(C& c, const ::sc_weapon_points_request& v) {
    c.scalar(v.kind, 4);
    c.scalar(v.amount, 4);
    c.scalar(v.expected_gained, 4);
}
inline const char* native_namespace(const ::sc_weapon_points_request& v) { return v.namespace_id; }
template<> struct NativeBinding<NativeCampaign> { using Result = ::sc_campaign_result; static constexpr CommandId id = CommandId::campaign_row; };
template<class C> void native_transfer(C& c, const NativeCampaign& v) {
    c.scalar(v.request.revision, 8);
    c.scalar(v.request.index, 4);
    c.scalar(v.request.count, 4);
    c.scalar(v.request.row.id, 4);
    c.scalar(v.request.row.flags, 4);
    c.scalar(v.request.row.native_index, 4);
    for (size_t i = 0; i < 192; ++i) c.scalar(v.request.row.map[i], 1);
    for (size_t i = 0; i < 96; ++i) c.scalar(v.request.row.title[i], 1);
    c.scalar(v.request.reward_presentation, 4);
    v1_transfer_campaign_summary(c, v.summary);
    for (const auto& challenge : v.summary.challenges) v1_transfer_campaign_challenge(c, challenge);
    v1_transfer_campaign_rewards_header(c, v.rewards);
    if (v.rewards.count > std::size(v.rewards.entries)) { c.valid = false; return; }
    for (size_t i = 0; i < v.rewards.count; ++i) v1_transfer_campaign_reward(c, v.rewards.entries[i]);
}
inline const char* native_namespace(const NativeCampaign& v) { return v.request.namespace_id; }
template<> struct NativeBinding<::sc_inventory_request> { using Result = ::sc_inventory_result; static constexpr CommandId id = CommandId::inventory; };
template<class C> void native_transfer(C& c, const ::sc_inventory_request& v) {
    c.scalar(v.kind, 4);
    c.scalar(v.weapons, 4);
    c.scalar(v.equipment, 4);
    c.scalar(v.special_weapons, 4);
    c.scalar(v.persistent_upgrades, 4);
    c.scalar(v.health_tier, 1);
    c.scalar(v.armor_tier, 1);
    c.scalar(v.ammo_tier, 1);
    c.scalar(v.reserved, 1);
}
inline const char* native_namespace(const ::sc_inventory_request& v) { return v.namespace_id; }
template<> struct NativeBinding<::sc_arsenal_request> { using Result = ::sc_arsenal_result; static constexpr CommandId id = CommandId::arsenal; };
template<class C> void native_transfer(C& c, const ::sc_arsenal_request& v) {
    c.scalar(v.kind, 4);
    c.scalar(v.mods, 4);
    c.scalar(v.upgrades, 4);
    c.scalar(v.masteries, 4);
    c.scalar(v.select_weapon, 1);
    c.scalar(v.select_mod, 1);
    c.scalar(v.challenge_index, 2);
    c.scalar(v.challenge_progress, 4);
    c.scalar(v.challenge_completed, 1);
    for (size_t i = 0; i < 3; ++i) c.scalar(v.reserved[i], 1);
}
inline const char* native_namespace(const ::sc_arsenal_request& v) { return v.namespace_id; }
template<> struct NativeBinding<::sc_runes_request> { using Result = ::sc_runes_result; static constexpr CommandId id = CommandId::runes; };
template<class C> void native_transfer(C& c, const ::sc_runes_request& v) {
    c.scalar(v.kind, 4);
    c.scalar(v.normal_runes, 4);
    c.scalar(v.support_runes, 4);
    c.scalar(v.select_slot, 1);
    c.scalar(v.select_rune, 1);
    for (size_t i = 0; i < 2; ++i) c.scalar(v.reserved[i], 1);
}
inline const char* native_namespace(const ::sc_runes_request& v) { return v.namespace_id; }
template<> struct NativeBinding<::sc_special_request> { using Result = ::sc_special_result; static constexpr CommandId id = CommandId::special; };
template<class C> void native_transfer(C& c, const ::sc_special_request& v) {
    c.scalar(v.kind, 4);
    c.scalar(v.own_crucible, 4);
    c.scalar(v.own_hammer, 4);
    c.scalar(v.hammer_tier, 4);
    c.scalar(v.selected, 4);
    c.scalar(v.refill_balance, 4);
    c.scalar(v.refill_flags, 4);
    c.scalar(v.refill_request_id, 8);
    c.scalar(v.refill_authorize, 4);
    c.scalar(v.reserved0, 4);
}
inline const char* native_namespace(const ::sc_special_request& v) { return v.namespace_id; }
template<> struct NativeBinding<::sc_deathlink_request> { using Result = ::sc_deathlink_result; static constexpr CommandId id = CommandId::deathlink; };
template<class C> void native_transfer(C& c, const ::sc_deathlink_request& v) {
    c.scalar(v.kind, 4);
    c.scalar(v.enabled, 4);
    c.scalar(v.mode, 4);
    c.scalar(v.flags, 4);
    c.scalar(v.event_id, 8);
    for (size_t i = 0; i < 16; ++i) c.scalar(v.event_hash[i], 1);
    c.scalar(v.ack_sequence, 8);
    c.scalar(v.reserved0, 4);
}
inline const char* native_namespace(const ::sc_deathlink_request& v) { return v.namespace_id; }
template<> struct NativeBinding<::sc_automap_request> { using Result = ::sc_automap_result; static constexpr CommandId id = CommandId::automap; };
template<class C> void native_transfer(C& c, const ::sc_automap_request& v) {
    c.scalar(v.kind, 4);
    c.scalar(v.known, 4);
    c.scalar(v.revision, 8);
    for (size_t i = 0; i < 9; ++i) c.scalar(v.checked_locations[i], 8);
}
inline const char* native_namespace(const ::sc_automap_request& v) { return v.namespace_id; }
template<> struct NativeBinding<::sc_command_request> { using Result = ::sc_command_result; static constexpr CommandId id = CommandId::command; };
template<class C> void native_transfer(C& c, const ::sc_command_request& v) {
    c.scalar(v.kind, 4);
    for (size_t i = 0; i < 2048; ++i) c.scalar(v.text[i], 1);
}
inline const char* native_namespace(const ::sc_command_request& v) { return v.namespace_id; }
template<> struct NativeBinding<::sc_combat_query> { using Result = ::sc_combat_snapshot; static constexpr CommandId id = CommandId::combat; };
template<> struct NativeBinding<::sc_combat_presentation_request> { using Result = ::sc_combat_presentation_result; static constexpr CommandId id = CommandId::combat_presentation; };
template<class C> void native_transfer(C& c, const ::sc_combat_presentation_request& v) {
    c.scalar(v.total, 8);
    c.scalar(v.target, 8);
    c.scalar(v.first_milestone, 8);
    c.scalar(v.milestone, 8);
    c.scalar(v.milestone_step, 8);
    c.scalar(v.visible, 4);
}
inline const char* native_namespace(const ::sc_combat_presentation_request& v) { return v.namespace_id; }
using Payload = std::variant<inspect_payload, engine_payload, context_payload, native_payload, diagnostic_payload, diagnostic_detail_payload, save_payload, save_admission_payload, save_write_payload, save_backup_payload, save_installation_payload, weapon_points_payload, campaign_row_payload_revision1, campaign_row_payload_revision2, campaign_row_payload, campaign_commit_payload, campaign_payload, inventory_payload, arsenal_payload, runes_payload, special_payload, deathlink_payload, automap_payload, command_payload, combat_payload, combat_presentation_payload, support_dump_payload>;
using Result = std::variant<sc_status, sc_engine_snapshot, sc_context_snapshot, sc_native_snapshot, sc_diagnostic_result, diagnostic_detail_result, sc_save_snapshot, save_admission_result, sc_save_write_snapshot, sc_save_backup_snapshot, sc_save_installation_snapshot, sc_weapon_points_result, sc_campaign_result, sc_inventory_result, sc_arsenal_result, sc_runes_result, sc_special_result, sc_deathlink_result, sc_automap_result, sc_command_result, sc_combat_snapshot, sc_combat_presentation_result>;
enum class Admission { P, G, S, D };
enum class Scope { process, gameplay, save, gameplay_diagnostic };
enum class NamespaceRule { process_zero, admitted_save, none };
enum class GenerationRule { none, exact_nonzero, exact_allow_zero };
enum class BindingRule { none, exact };
struct AdmissionPolicy { Scope scope; NamespaceRule namespace_rule; GenerationRule generation; BindingRule save_binding; };
inline constexpr AdmissionPolicy admission_policies[] = {
    {Scope::process, NamespaceRule::process_zero, GenerationRule::none, BindingRule::none},
    {Scope::gameplay, NamespaceRule::admitted_save, GenerationRule::exact_nonzero, BindingRule::exact},
    {Scope::save, NamespaceRule::admitted_save, GenerationRule::exact_allow_zero, BindingRule::exact},
    {Scope::gameplay_diagnostic, NamespaceRule::none, GenerationRule::exact_nonzero, BindingRule::none},
};
enum class Execution { snapshot, publication, native_frame, native_frame_async };
enum class Evidence { feature_result, accepted_publication, dispatch_only };
enum class Ack { none, terminal_consumed };
struct VariantPolicy { uint32_t tag; Operation operation; Admission admission; Execution execution; const char* effect; const char* availability; };
struct Descriptor { uint16_t id, revision; const char* name; const char* metadata; Admission admission; Execution execution; Evidence evidence; Ack ack; const VariantPolicy* variants; size_t variant_count; bool (*decode_payload)(const Bytes&, Payload&); bool (*encode_payload)(const Payload&, Bytes&); bool (*decode_result)(const Bytes&, Result&); bool (*encode_result)(const Result&, Bytes&); bool (*native_matches)(const NativeRequest&); };
extern const Descriptor descriptors[];
extern const size_t descriptor_count;
template<class Factory> auto registry_handlers(Factory factory) { return std::array{
    factory.template entry<CommandId::inspect>(),
    factory.template entry<CommandId::engine>(),
    factory.template entry<CommandId::context>(),
    factory.template entry<CommandId::native>(),
    factory.template entry<CommandId::diagnostic>(),
    factory.template entry<CommandId::diagnostic_detail>(),
    factory.template entry<CommandId::save>(),
    factory.template entry<CommandId::save_admission>(),
    factory.template entry<CommandId::save_write>(),
    factory.template entry<CommandId::save_backup>(),
    factory.template entry<CommandId::save_installation>(),
    factory.template entry<CommandId::weapon_points>(),
    factory.template entry<CommandId::campaign_row>(),
    factory.template entry<CommandId::campaign_commit>(),
    factory.template entry<CommandId::campaign>(),
    factory.template entry<CommandId::inventory>(),
    factory.template entry<CommandId::arsenal>(),
    factory.template entry<CommandId::runes>(),
    factory.template entry<CommandId::special>(),
    factory.template entry<CommandId::deathlink>(),
    factory.template entry<CommandId::automap>(),
    factory.template entry<CommandId::command>(),
    factory.template entry<CommandId::combat>(),
    factory.template entry<CommandId::combat_presentation>(),
    factory.template entry<CommandId::support_dump>(),
}; }
}
