// The checked-in fixtures specify bytes independently of the codec under test.
#include "protocol.h"
#include "sentinel_protocol_generated.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "wire_fixtures.h"

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL protocol_golden:%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace sentinel;
namespace {
sc_native_scope scope() {
    sc_native_scope value{}; value.pid = 0x01020304; value.process_created = 0x0102030405060708;
    value.lifecycle_generation = 0x1112131415161718;
    for (unsigned i = 0; i < 16; ++i) value.instance_id[i] = static_cast<uint8_t>(i);
    return value;
}
sc_diagnostic_request request() {
    sc_diagnostic_request value{}; value.expected = scope(); value.request_id = 0x2122232425262728;
    value.deadline_ms = 1000;
    for (unsigned i = 0; i < 16; ++i) value.nonce[i] = static_cast<uint8_t>(16 + i);
    return value;
}
Snapshot identity() {
    Snapshot value{}; value.pid = scope().pid; value.process_created = scope().process_created;
    const auto source = scope();
    std::copy(std::begin(source.instance_id), std::end(source.instance_id), value.instance.begin());
    value.core.abi_version = 1; strcpy_s(value.core.version, "1.0.0");
    std::memset(value.core.build_id, 'b', 64); return value;
}
sc_diagnostic_result execution() {
    sc_diagnostic_result value{}; value.scope = scope(); value.request_id = request().request_id;
    std::memcpy(value.nonce, request().nonce, 16);
    value.state = SC_DIAGNOSTIC_EXECUTED;
    value.admitted_at_ms = 100; value.deadline_at_ms = 1100; value.claimed_at_ms = 110;
    value.observed_at_ms = 120; value.executed_at_ms = 130; value.completed_at_ms = 140;
    value.thread_id = 0x11223344; value.site_revision = 1; value.phase = 1;
    value.lifecycle = SC_LIFETIME_ACTIVE; value.game_state = SC_GAME_IN_GAME;
    value.current_map.validity = SC_OBSERVATION_OBSERVED; value.current_map.length = 12;
    strcpy_s(value.current_map.bytes, "game/hub/hub"); return value;
}
sc_command_request command() {
    sc_command_request value{}; value.execution = request(); value.kind = SC_COMMAND_AMMO_REFILL;
    std::memset(value.namespace_id, 'a', 64); strcpy_s(value.text, "give ammo"); return value;
}
sc_command_result command_result() {
    sc_command_result value{}; value.size = sizeof(value); value.abi_version = 1;
    value.execution = execution(); value.kind = SC_COMMAND_AMMO_REFILL; value.outcome = SC_COMMAND_DISPATCHED;
    std::memset(value.namespace_id, 'a', 64); return value;
}
sc_inventory_result inventory() {
    sc_inventory_result value{}; value.size = sizeof(value); value.abi_version = 8;
    value.execution = execution(); std::memset(value.namespace_id, 'a', 64);
    value.kind = SC_INV_ENSURE; value.outcome = SC_INV_MUTATED; value.flags = 15;
    value.weapons_before = 1; value.weapons_after = 5;
    value.equipment_before = value.equipment_after = UINT32_MAX; value.upgrades_after = 2048;
    value.health_tier_after = 1; value.armor_tier_before = 2; value.armor_tier_after = 3;
    value.ammo_tier_before = 3; value.ammo_tier_after = 4; value.operations_applied = 3;
    value.ice_bomb_after = 1; value.dash_before = value.dash_after = 1;
    value.slayer_key_before = 255; value.frag_grenade_before = value.frag_grenade_after = 1;
    value.flame_belch_before = value.flame_belch_after = 255; value.blood_punch_after = 1;
    value.perks_known = 0x8000000000000001; value.perks_owned = 0x8000000000000000;
    return value;
}
struct Fixture { Message bytes{}; size_t size = 0; };
Fixture read(const std::filesystem::path& path, const std::string& name) {
    const auto bytes = wire_fixtures::read(path).at(name).bytes;
    CHECK(!bytes.empty() && bytes.size() <= max_message);
    Fixture value{}; value.size = bytes.size();
    std::copy(bytes.begin(), bytes.end(), value.bytes.begin()); return value;
}

void matches(const Fixture& fixture, const Message& actual, size_t size) {
    CHECK(size == fixture.size);
    CHECK(std::equal(actual.begin(), actual.begin() + size, fixture.bytes.begin()));
}
WireResult decode_command(const Fixture& fixture, sc_command_request& value) {
    uint16_t operation = 0;
    const auto result = decode_request(fixture.bytes, fixture.size, &operation, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &value);
    CHECK(result != WireResult::ok || operation == command_submit_operation); return result;
}
void test_submit(const std::filesystem::path& root) {
    const auto fixture = read(root, "command_submit"); Message actual{};
    matches(fixture, actual, encode_command_request(actual, command_submit_operation, command()));
    sc_command_request decoded{}; CHECK(decode_command(fixture, decoded) == WireResult::ok);
    matches(fixture, actual, encode_command_request(actual, command_submit_operation, decoded));
    auto bad = fixture; --bad.size; CHECK(decode_command(bad, decoded) == WireResult::malformed);
    bad = fixture; bad.bytes[0] ^= 1;
    CHECK(decode_request(bad.bytes, bad.size) == WireResult::malformed);
    bad = fixture; bad.bytes[2208] = 1; CHECK(decode_command(bad, decoded) == WireResult::malformed);
}
void test_command_result(const std::filesystem::path& root) {
    const auto fixture = read(root, "command_result"); Message actual{};
    matches(fixture, actual, encode_command_response(actual, WireResult::ok, command_result_operation, identity(), command_result()));
    WireResult status{}; Snapshot source{}; sc_command_result decoded{};
    CHECK(decode_command_response(fixture.bytes, fixture.size, status, command_result_operation, source, decoded));
    CHECK(status == WireResult::ok);
    matches(fixture, actual, encode_command_response(actual, status, command_result_operation, source, decoded));
    CHECK(!decode_command_response(fixture.bytes, fixture.size - 1, status, command_result_operation, source, decoded));
    auto bad = fixture; bad.bytes[12] = 255;
    CHECK(!decode_command_response(bad.bytes, bad.size, status, command_result_operation, source, decoded));
}
void test_inventory_result(const std::filesystem::path& root) {
    const auto fixture = read(root, "inventory_result"); Message actual{};
    matches(fixture, actual, encode_inventory_response(actual, WireResult::ok, inventory_result_operation, identity(), inventory()));
    WireResult status{}; Snapshot source{}; sc_inventory_result decoded{};
    CHECK(decode_inventory_response(fixture.bytes, fixture.size, status, inventory_result_operation, source, decoded));
    CHECK(status == WireResult::ok);
    matches(fixture, actual, encode_inventory_response(actual, status, inventory_result_operation, source, decoded));
    CHECK(!decode_inventory_response(fixture.bytes, fixture.size - 1, status, inventory_result_operation, source, decoded));
    auto bad = fixture; bad.bytes[129] = 7;
    CHECK(!decode_inventory_response(bad.bytes, bad.size, status, inventory_result_operation, source, decoded));
}
void test_inspect_and_headers(const std::filesystem::path& root) {
    const auto fixture = read(root, "inspect_request"); Message actual{};
    matches(fixture, actual, encode_request(actual, inspect_capability));
    CHECK(decode_request(fixture.bytes, fixture.size) == WireResult::ok);
    auto bad = fixture; bad.bytes[4] = 2;
    CHECK(decode_request(bad.bytes, bad.size) == WireResult::incompatible_protocol);
    bad = fixture; bad.bytes[6] = 52;
    CHECK(decode_request(bad.bytes, bad.size) == WireResult::unsupported_operation);
    bad = fixture; bad.bytes[16] = 2;
    CHECK(decode_request(bad.bytes, bad.size) == WireResult::capability_unavailable);
    for (const size_t offset : {size_t(0), size_t(8), size_t(12)}) {
        bad = fixture; bad.bytes[offset] ^= 1;
        CHECK(decode_request(bad.bytes, bad.size) == WireResult::malformed);
    }
    CHECK(decode_request(fixture.bytes, header_size - 1) == WireResult::malformed);
    Snapshot snapshot{}; WireResult status{};
    for (uint32_t code = 1; code <= 4; ++code) {
        actual = fixture.bytes; actual[8] = 0; actual[12] = static_cast<uint8_t>(code);
        CHECK(decode_response(actual, header_size, status, snapshot));
        CHECK(static_cast<uint32_t>(status) == code);
        CHECK(!decode_response(actual, fixture.size, status, snapshot));
        matches(Fixture{actual, header_size}, actual, encode_response(actual, status, snapshot));
    }
}
void test_detail(const std::filesystem::path& root) {
    const auto fixture = read(root, "diagnostic_detail_submit"); Message actual{};
    matches(fixture, actual, encode_native_request(actual, diagnostic_detail_submit_operation, request()));
    sc_diagnostic_request value{}; uint16_t operation = 0;
    CHECK(decode_request(fixture.bytes, fixture.size, &operation, &value) == WireResult::ok);
    CHECK(operation == diagnostic_detail_submit_operation);
    matches(fixture, actual, encode_native_request(actual, operation, value));
    auto bad = fixture; bad.bytes[16] = 16;
    CHECK(decode_request(bad.bytes, bad.size) == WireResult::capability_unavailable);
    bad = fixture; std::fill(bad.bytes.begin() + 84, bad.bytes.begin() + 88, uint8_t{0});
    CHECK(decode_request(bad.bytes, bad.size) == WireResult::malformed);
}
WireResult decode_campaign(const Fixture& fixture, sc_campaign_request& value,
                           sc_campaign_summary& summary, sc_campaign_rewards& rewards) {
    return decode_request(fixture.bytes, fixture.size, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, &value, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &summary, &rewards);
}
void test_campaign(const std::filesystem::path& root) {
    const auto fixture = read(root, "campaign_rewards_row"); Message actual{};
    sc_campaign_request value{}; value.execution = request(); std::memset(value.namespace_id, 'a', 64);
    value.revision = 7; value.count = 1; value.row.id = 42; value.row.flags = 3; value.row.native_index = 2;
    strcpy_s(value.row.map, "game/hub/hub"); strcpy_s(value.row.title, "Golden mission");
    sc_campaign_summary summary{}; summary.known = 1; summary.found = 1; summary.total = 2; summary.band = 1;
    strcpy_s(summary.challenges[0].unlockable, "mission_challenge/test");
    summary.challenges[0].found = 1; summary.challenges[0].required = 2;
    sc_campaign_rewards rewards{}; rewards.known = 1; rewards.count = 1;
    auto& reward = rewards.entries[0]; reward.location_id = 7770001; reward.kind = 1; reward.checked = 1;
    strcpy_s(reward.unlockable, "reward/test"); strcpy_s(reward.name, "Recompensa \xc3\xa1");
    strcpy_s(reward.text, "Descri\xc3\xa7\xc3\xa3o");
    matches(fixture, actual, encode_campaign_request(actual, campaign_row_operation, value, &summary, &rewards));
    sc_campaign_request decoded{}; sc_campaign_summary ds{}; sc_campaign_rewards dr{};
    CHECK(decode_campaign(fixture, decoded, ds, dr) == WireResult::ok);
    CHECK(decoded.reward_presentation == 1 && dr.count == 1 && ds.challenges[0].required == 2);
    matches(fixture, actual, encode_campaign_request(actual, campaign_row_operation, decoded, &ds, &dr));
    auto bad = fixture; bad.bytes[765] = 18;
    CHECK(decode_campaign(bad, decoded, ds, dr) == WireResult::malformed);
    bad = fixture; bad.bytes[781] = 255;
    CHECK(decode_campaign(bad, decoded, ds, dr) == WireResult::malformed);
}
void test_campaign_extensions(const std::filesystem::path& root) {
    const auto full = read(root, "campaign_rewards_row");
    for (const size_t payload : {size_t(453), size_t(745)}) {
        auto fixture = full; fixture.size = header_size + payload;
        fixture.bytes[8] = static_cast<uint8_t>(payload); fixture.bytes[9] = static_cast<uint8_t>(payload >> 8);
        const uint64_t capability = payload == 453 ? campaign_menu_capability : campaign_presentation_capability;
        for (size_t i = 0; i < 8; ++i) fixture.bytes[16 + i] = static_cast<uint8_t>(capability >> (i * 8));
        sc_campaign_request value{}; sc_campaign_summary summary{}; sc_campaign_rewards rewards{}; Message actual{};
        CHECK(decode_campaign(fixture, value, summary, rewards) == WireResult::ok);
        CHECK(value.reward_presentation == 0 && rewards.count == 0);
        matches(fixture, actual, encode_campaign_request(actual, campaign_row_operation, value, payload == 745 ? &summary : nullptr));
        const generated::Bytes input(fixture.bytes.begin(), fixture.bytes.begin() + fixture.size);
        generated::Bytes output; uint32_t status = 0;
        if (payload == 453) {
            generated::v1_campaign_base projected{};
            CHECK(generated::decode_campaign_base(input, projected, status));
            CHECK(generated::encode_campaign_base(projected, output) && output == input);
        } else {
            generated::v1_campaign_presentation projected{};
            CHECK(generated::decode_campaign_presentation(input, projected, status));
            CHECK(generated::encode_campaign_presentation(projected, output) && output == input);
        }
        auto bad = fixture; --bad.size;
        CHECK(decode_campaign(bad, value, summary, rewards) == WireResult::malformed);
    }
}
void test_automap(const std::filesystem::path& root) {
    const auto fixture = read(root, "automap_publish"); Message actual{};
    CHECK(fixture.size == header_size + 225);
    sc_automap_request value{}; value.execution = request(); std::memset(value.namespace_id, 'a', 64);
    value.kind = SC_AUTOMAP_PUBLISH; value.known = 1; value.revision = 9;
    value.checked_locations[0] = value.checked_locations[8] = 0x8000000000000001;
    matches(fixture, actual, encode_automap_request(actual, value));
    sc_automap_request decoded{};
    CHECK(decode_request(fixture.bytes, fixture.size, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &decoded) == WireResult::ok);
    CHECK(decoded.checked_locations[8] == 0x8000000000000001);
    matches(fixture, actual, encode_automap_request(actual, decoded));
    auto bad = fixture; bad.size -= 8; bad.bytes[8] = 217;
    CHECK(decode_request(bad.bytes, bad.size) == WireResult::malformed);
}
void test_signed_rune() {
    sc_runes_request value{}; value.execution = request(); std::memset(value.namespace_id, 'a', 64);
    value.kind = SC_RUNES_SELECT_NORMAL; value.select_slot = 2; value.select_rune = -1;
    Message bytes{}; const auto size = encode_runes_request(bytes, runes_submit_operation, value);
    CHECK(size == 169 && bytes[165] == 2 && bytes[166] == 255);
    sc_runes_request decoded{};
    CHECK(decode_request(bytes, size, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, &decoded) == WireResult::ok);
    CHECK(decoded.select_rune == -1);
    generated::v1_runes_request projected{}; uint32_t status = 0;
    generated::Bytes input(bytes.begin(), bytes.begin() + size), output;
    CHECK(generated::decode_runes_request(input, projected, status));
    CHECK(projected.select_rune == -1);
    CHECK(generated::encode_runes_request(projected, output) && output == input);
}
template<class T, class Decode, class Encode>
void generated_fixture(const std::filesystem::path& root, const char* name, Decode decode, Encode encode) {
    const auto fixture = read(root, name);
    const generated::Bytes input(fixture.bytes.begin(), fixture.bytes.begin() + fixture.size);
    generated::Bytes output; T value{}; uint32_t status = 0;
    CHECK(decode(input, value, status)); CHECK(status == 0);
    CHECK(encode(value, output, status)); CHECK(output == input);
    auto bad = input; bad.pop_back(); CHECK(!decode(bad, value, status));
}
void test_generated(const std::filesystem::path& root) {
    using namespace generated;
    generated_fixture<v1_inspect_request>(root, "inspect_request", decode_inspect_request, encode_inspect_request);
    generated_fixture<v1_diagnostic_detail_submit>(root, "diagnostic_detail_submit", decode_diagnostic_detail_submit, encode_diagnostic_detail_submit);
    generated_fixture<v1_command_submit>(root, "command_submit", decode_command_submit, encode_command_submit);
    generated_fixture<v1_command_result>(root, "command_result", decode_command_result, encode_command_result);
    generated_fixture<v1_inventory_result>(root, "inventory_result", decode_inventory_result, encode_inventory_result);
    generated_fixture<v1_campaign_rewards_row>(root, "campaign_rewards_row", decode_campaign_rewards_row, encode_campaign_rewards_row);
    generated_fixture<v1_automap_publish>(root, "automap_publish", decode_automap_publish, encode_automap_publish);
    CHECK(descriptor_count == 31);
}
void test_generated_boundaries(const std::filesystem::path& root) {
    const auto fixture = read(root, "command_result");
    const generated::Bytes bytes(fixture.bytes.begin(), fixture.bytes.begin() + fixture.size);
    for (uint32_t code = 1; code <= 4; ++code) {
        generated::v1_command_result value{}; generated::Bytes encoded;
        CHECK(generated::encode_command_result(value, encoded, code));
        CHECK(encoded.size() == header_size && encoded[12] == code);
        uint32_t result = 0;
        CHECK(generated::decode_command_result(encoded, value, result) && result == code);
        encoded.push_back(0); CHECK(!generated::decode_command_result(encoded, value, result));
    }
    for (const size_t offset : {size_t(0), size_t(4), size_t(6), size_t(8), size_t(12), size_t(16)}) {
        auto bad = bytes; bad[offset] = 255;
        generated::v1_command_result value{}; uint32_t result = 0;
        CHECK(!generated::decode_command_result(bad, value, result));
    }
    generated::weapon_points_grant_payload grant{}; grant.amount = 1; grant.expected_gained = INT32_MAX;
    generated::Bytes output;
    CHECK(!generated::encode(grant, output));
    grant.expected_gained = INT32_MAX - 1;
    CHECK(generated::encode(grant, output));
    generated::runes_payload selection{};
    CHECK(!generated::decode(generated::Bytes{255, 255, 255, 255}, selection));
}
void test_generated_campaign_constraints(const std::filesystem::path& root) {
    const auto fixture = read(root, "campaign_rewards_row");
    const generated::Bytes bytes(fixture.bytes.begin(), fixture.bytes.begin() + fixture.size);
    generated::v1_campaign_rewards_row value{}; uint32_t result = 0;
    CHECK(generated::decode_campaign_rewards_row(bytes, value, result));
    value.rewards.push_back(value.rewards[0]); generated::Bytes output;
    CHECK(!generated::encode_campaign_rewards_row(value, output));
    sc_campaign_request legacy{}; sc_campaign_summary summary{}; sc_campaign_rewards rewards{};
    CHECK(decode_campaign(fixture, legacy, summary, rewards) == WireResult::ok);
    legacy.execution.expected.lifecycle_generation = 0; Message actual{};
    const auto size = encode_campaign_request(actual, campaign_row_operation, legacy, &summary, &rewards);
    CHECK(generated::decode_campaign_rewards_row(generated::Bytes(actual.begin(), actual.begin() + size), value, result));
    CHECK(value.expected_generation == 0);
}
void put(Fixture& value, size_t offset, uint64_t n, size_t width) {
    for (size_t i = 0; i < width; ++i) { value.bytes[offset + i] = static_cast<uint8_t>(n); n >>= 8; }
}
size_t encode_payload(Message& out, const DecodedRequest& value, std::monostate) {
    if (value.operation == native_operation) return encode_native_request(out, value.operation, {}, value.cursor);
    if (value.operation >= diagnostic_submit_operation && value.operation <= diagnostic_detail_cancel_operation)
        return encode_native_request(out, value.operation, value.execution);
    if (value.operation == save_write_operation) return encode_save_write_request(out, value.cursor);
    constexpr uint64_t caps[] = {0, 1, 2, 4, 8, 16, 16, 16, 32, 32, 32, 64, 128, 256, 512, 512, 512, 1024};
    CHECK(value.operation < std::size(caps));
    return encode_request(out, caps[value.operation], 1, value.operation);
}
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_save_backup_request& p) { return encode_backup_request(out, v.operation, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_weapon_points_request& p) { return encode_weapon_points_request(out, v.operation, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_inventory_request& p) { return encode_inventory_request(out, v.operation, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_arsenal_request& p) { return encode_arsenal_request(out, v.operation, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_runes_request& p) { return encode_runes_request(out, v.operation, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_special_request& p) { return encode_special_request(out, v.operation, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_deathlink_request& p) { return encode_deathlink_request(out, v.operation, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_command_request& p) { return encode_command_request(out, v.operation, p); }
size_t encode_payload(Message& out, const DecodedRequest&, const sc_automap_request& p) { return encode_automap_request(out, p); }
size_t encode_payload(Message& out, const DecodedRequest&, const sc_combat_presentation_request& p) { return encode_combat_presentation_request(out, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const sc_combat_query& p) { return encode_combat_request(out, v.execution.expected.pid, p); }
size_t encode_payload(Message& out, const DecodedRequest& v, const CampaignRequest& p) {
    const auto* summary = v.capability == 4096 ? nullptr : &p.summary;
    const auto* rewards = v.capability == 1048576 ? &p.rewards : nullptr;
    return encode_campaign_request(out, v.operation, p.request, summary, rewards);
}
void test_request_boundaries(const Fixture& fixture, uint16_t op) {
    DecodedRequest value{};
    for (size_t size = 0; size < fixture.size; ++size)
        CHECK(decode_request(fixture.bytes, size, value) == WireResult::malformed);
    auto bad = fixture;
    put(bad, 12, 1, 4); CHECK(decode_request(bad.bytes, bad.size, value) == WireResult::malformed);
    bad = fixture; put(bad, 16, 0, 8);
    const bool subset = op <= 3 || op == 11 || op == 12;
    CHECK(decode_request(bad.bytes, bad.size, value) == (subset ? WireResult::ok : WireResult::capability_unavailable));
    bad = fixture; put(bad, 16, UINT64_MAX, 8);
    CHECK(decode_request(bad.bytes, bad.size, value) == WireResult::capability_unavailable);
    bad = fixture; ++bad.size; put(bad, 8, bad.size - header_size, 4);
    CHECK(decode_request(bad.bytes, bad.size, value) == WireResult::malformed);
}
void test_request_envelope(const Fixture& fixture, uint16_t op) {
    if (op < 5 || op == 11 || op == 12 || op == 13 || op == 17 || op == 50) return;
    DecodedRequest value{};
    constexpr size_t offsets[] = {24, 28, 36, 52, 60, 68, 84};
    constexpr size_t widths[] = {4, 8, 16, 8, 8, 16, 4};
    for (size_t i = 0; i < std::size(offsets); ++i) {
        auto bad = fixture;
        std::fill_n(bad.bytes.begin() + offsets[i], widths[i], uint8_t{0});
        const bool generation = offsets[i] == 52 && op >= 22 && op <= 24;
        CHECK(decode_request(bad.bytes, bad.size, value) == (generation ? WireResult::ok : WireResult::malformed));
    }
    auto bad = fixture; put(bad, 84, 5001, 4);
    CHECK(decode_request(bad.bytes, bad.size, value) == WireResult::malformed);
    put(bad, 84, 5000, 4); CHECK(decode_request(bad.bytes, bad.size, value) == WireResult::ok);
    put(bad, 84, 1, 4); CHECK(decode_request(bad.bytes, bad.size, value) == WireResult::ok);
}
void test_all_requests(const std::filesystem::path& root) {
    for (const auto& [name, entry] : wire_fixtures::read(root)) {
        if (entry.response) continue;
        const auto fixture = read(root, name);
        const auto op = static_cast<uint16_t>(fixture.bytes[6]);
        DecodedRequest value{};
        CHECK(decode_request(fixture.bytes, fixture.size, value) == WireResult::ok);
        CHECK(value.operation == op);
        Message actual{};
        const auto size = std::visit([&](const auto& payload) { return encode_payload(actual, value, payload); }, value.payload);
        matches(fixture, actual, size);
        test_request_boundaries(fixture, op); test_request_envelope(fixture, op);
    }
}

template<class T, class Encode, class Decode>
void check_response(const std::filesystem::path& root, const char* name, Encode encode, Decode decode) {
    const auto filename = std::string(name) == "inventory" ? "inventory_result" :
        std::string(name) == "command" ? "command_result" : std::string("response_") + name;
    const auto fixture = read(root, filename);
    Message actual{}; Snapshot snapshot{}; T value{}; WireResult code{};
    std::printf("response %s\n", name);
    CHECK(decode(fixture.bytes, fixture.size, code, snapshot, value));
    CHECK(code == WireResult::ok);
    CHECK(snapshot.pid == scope().pid && snapshot.process_created == scope().process_created);
    CHECK(!std::memcmp(snapshot.instance.data(), scope().instance_id, 16));
    matches(fixture, actual, encode(actual, code, snapshot, value));
    for (size_t size = 0; size < fixture.size; ++size) CHECK(!decode(fixture.bytes, size, code, snapshot, value));
    for (size_t offset : {size_t(0), size_t(4), size_t(6), size_t(8), size_t(12)}) {
        auto bad = fixture; bad.bytes[offset] = 255;
        CHECK(!decode(bad.bytes, bad.size, code, snapshot, value));
    }
    for (uint32_t error = 1; error <= 4; ++error) {
        auto bad = fixture; put(bad, 8, 0, 4); put(bad, 12, error, 4);
        CHECK(decode(bad.bytes, header_size, code, snapshot, value));
        CHECK(static_cast<uint32_t>(code) == error);
        matches(Fixture{bad.bytes, header_size}, actual, encode(actual, code, snapshot, value));
    }
}
template<class T>
void check_fixed_response(const std::filesystem::path& root, const char* name,
    size_t (*encode)(Message&, WireResult, const Snapshot&, const T&),
    bool (*decode)(const Message&, size_t, WireResult&, Snapshot&, T&)) {
    check_response<T>(root, name, encode, decode);
}
template<class T>
void check_operation_response(const std::filesystem::path& root, const char* name, uint16_t op,
    size_t (*encode)(Message&, WireResult, uint16_t, const Snapshot&, const T&),
    bool (*decode)(const Message&, size_t, WireResult&, uint16_t, Snapshot&, T&)) {
    check_response<T>(root, name,
        [=](Message& m, WireResult c, const Snapshot& s, const T& v) { return encode(m, c, op, s, v); },
        [=](const Message& m, size_t n, WireResult& c, Snapshot& s, T& v) { return decode(m, n, c, op, s, v); });
}
void test_fixed_responses(const std::filesystem::path& root) {
    check_fixed_response(root, "write", encode_save_write_response, decode_save_write_response);
    check_fixed_response(root, "automap", encode_automap_response, decode_automap_response);
    check_fixed_response(root, "combat", encode_combat_response, decode_combat_response);
    check_fixed_response(root, "presentation", encode_combat_presentation_response, decode_combat_presentation_response);
}
void test_operation_responses(const std::filesystem::path& root) {
    check_operation_response(root, "backup", 14, encode_backup_response, decode_backup_response);
    check_operation_response(root, "points", 18, encode_weapon_points_response, decode_weapon_points_response);
    check_operation_response(root, "campaign", 22, encode_campaign_response, decode_campaign_response);
    check_operation_response(root, "inventory", 26, encode_inventory_response, decode_inventory_response);
    check_operation_response(root, "arsenal", 29, encode_arsenal_response, decode_arsenal_response);
    check_operation_response(root, "runes", 33, encode_runes_response, decode_runes_response);
    check_operation_response(root, "special", 37, encode_special_response, decode_special_response);
    check_operation_response(root, "deathlink", 41, encode_deathlink_response, decode_deathlink_response);
    check_operation_response(root, "command", 47, encode_command_response, decode_command_response);
}
void test_native_responses(const std::filesystem::path& root) {
    for (const auto& pair : {std::pair{"native", uint16_t(4)}, {"detail", uint16_t(8)}}) {
        const auto fixture = read(root, (std::string("response_") + pair.first));
        Snapshot snapshot{}; sc_native_snapshot native{}; sc_diagnostic_result diagnostic{}; sc_diagnostic_detail detail{};
        WireResult code{}; Message actual{};
        std::printf("response %s\n", pair.first);
        CHECK(decode_native_response(fixture.bytes, fixture.size, code, pair.second, snapshot, native, diagnostic, &detail));
        matches(fixture, actual, encode_native_response(actual, code, pair.second, snapshot, native, diagnostic, detail));
        for (size_t size = 0; size < fixture.size; ++size)
            CHECK(!decode_native_response(fixture.bytes, size, code, pair.second, snapshot, native, diagnostic, &detail));
    }
    const auto fixture = read(root, "response_inspect"); Snapshot snapshot{}; WireResult code{}; Message actual{};
    CHECK(decode_response(fixture.bytes, fixture.size, code, snapshot));
    matches(fixture, actual, encode_response(actual, code, snapshot));
}

void test_campaign_boundaries(const std::filesystem::path& root) {
    const auto fixture = read(root, "campaign_rewards_row"); DecodedRequest decoded{};
    constexpr std::pair<size_t, uint32_t> invalid[] = {
        {469, 3}, {473, 3}, {481, 6}, {481, 10002u << 8 | 1}, {481, 1u << 8},
        {565, 3}, {573, 2}, {761, 2}, {765, 18}, {769, 0}, {773, 0}, {777, 2}, {169, 0}, {173, 512}
    };
    for (const auto& item : invalid) {
        auto bad = fixture; put(bad, item.first, item.second, 4);
        CHECK(decode_request(bad.bytes, bad.size, decoded) == WireResult::malformed);
    }
    for (const auto& prefix : {std::string("\xc0\x80"), std::string("\xed\xa0\x80"),
         std::string("\xf4\x90\x80\x80"), std::string("\xe0\x80\x80"), std::string("\x80")}) {
        auto bad = fixture; std::copy(prefix.begin(), prefix.end(), bad.bytes.begin() + 861);
        CHECK(decode_request(bad.bytes, bad.size, decoded) == WireResult::malformed);
    }
    auto accepted = fixture; accepted.bytes[861] = 0xc2; accepted.bytes[862] = 0x80; accepted.bytes[863] = 0;
    CHECK(decode_request(accepted.bytes, accepted.size, decoded) == WireResult::ok);
}

}
int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 2);
    test_all_requests(argv[1]);
    test_fixed_responses(argv[1]); test_operation_responses(argv[1]); test_native_responses(argv[1]);
    test_campaign_boundaries(argv[1]);
    test_submit(argv[1]); test_command_result(argv[1]); test_inventory_result(argv[1]);
    test_inspect_and_headers(argv[1]); test_detail(argv[1]); test_campaign(argv[1]);
    test_campaign_extensions(argv[1]); test_automap(argv[1]); test_signed_rune(); test_generated(argv[1]);
    test_generated_boundaries(argv[1]);
    test_generated_campaign_constraints(argv[1]);
    std::puts("PASS test_wire_fixture_matches_published_bytes");
}
