#include "combat.h"
#include "combat_presentation.h"
#include "protocol.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>

using namespace sentinel;
using namespace sentinel::combat;
namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << message << '\n'; }
}
Context context(uint64_t generation = 1) {
    Context out{};
    out.generation = generation; out.stamp = 20; out.sampled_at_ms = 100;
    out.world = 0x10000; out.player = 0x20000; out.owner_thread = 17;
    std::memset(out.namespace_id, 'a', 64);
    out.map.validity = SC_OBSERVATION_OBSERVED;
    constexpr char map[] = "game/sp/hellonearth/hellonearth";
    std::memcpy(out.map.bytes, map, sizeof(map));
    out.map.length = static_cast<uint32_t>(std::strlen(out.map.bytes));
    return out;
}
DeathRecord death(const Context& ctx) {
    DeathRecord record{};
    record.victim_spawn_id = 0x123456;
    record.attacker_spawn_id = 9; record.inflictor_spawn_id = 10;
    record.victim_type = 1; record.attacker_type = 0; record.attacker_player = ctx.player;
    record.victim_ai_type = 0x1234; record.death_time = 999960;
    record.game_mode = 1; record.attack_type = 0;
    return record;
}
template<class T> void write(std::array<uint8_t, 0x128>& bytes, size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
sc_combat_query query_for(const Context& ctx) {
    sc_combat_query query{};
    std::memcpy(query.namespace_id, ctx.namespace_id, 65);
    query.map_count = 1;
    std::memcpy(query.maps[0], ctx.map.bytes, SC_CONTEXT_MAP_CAPACITY);
    return query;
}
}
int main(int argc, char** argv) {
    auto model = std::make_unique<Model>();
    const auto ctx = context();
    model->transition(1); model->context(ctx);
    model->registered(ctx.world, 0x30000, 3, 0x123456);
    auto outer = model->capture(ctx.world, 0x30000, 3, 0x123456, 101, 20, 17);
    check(outer.registration_serial != 0, "initial registration missing");
    check(!model->confirmed_dead(ctx.world, 0x30000, 3, 0x123456), "live registration released a special slot");
    model->registered(ctx.world, 0x30000, 3, 0x123456);
    auto inner = model->capture(ctx.world, 0x30000, 3, 0x123456, 102, 20, 17);
    check(inner.registration_serial > outer.registration_serial, "forced ID reused registration identity");
    const auto record = death(ctx);
    check(model->publish(inner, record, 103, 20, 17) == Publication::published, "nested death lost");
    check(model->confirmed_dead(ctx.world, 0x30000, 3, 0x123456), "confirmed death retained its special slot");
    check(!model->confirmed_dead(ctx.world + 1, 0x30000, 3, 0x123456) &&
          !model->confirmed_dead(ctx.world, 0x30001, 3, 0x123456) &&
          !model->confirmed_dead(ctx.world, 0x30000, 3, 0x123457) &&
          !model->confirmed_dead(ctx.world, 0x30000, managed_slots, 0x123456),
          "another world, body or life reused a death");
    check(model->publish(outer, record, 104, 20, 17) == Publication::published, "outer frozen registration lost");
    check(model->publish(outer, record, 105, 20, 17) == Publication::duplicate, "duplicate death got a new sequence");
    const auto query = query_for(ctx);
    sc_combat_snapshot page{}; model->snapshot(query, page);
    check(page.qualified_kills == 2, "reentry changed frozen life identity");
    const auto missing = model->capture(ctx.world, 0x40000, 4, 0x123456, 106, 20, 17);
    check(model->publish(missing, record, 106, 20, 17) == Publication::missing_registration, "unobserved birth silently accepted");
    auto rejected = record;
    rejected.attack_type = 2;
    check(model->publish(inner, rejected, 106, 20, 17) == Publication::ignored, "glory start counted");
    rejected = record; rejected.game_mode = 2;
    check(model->publish(inner, rejected, 106, 20, 17) == Publication::ignored, "noncampaign death counted");
    rejected = record; rejected.attacker_player = 0x50000;
    check(model->publish(inner, rejected, 106, 20, 17) == Publication::duplicate, "attacker changed the same death identity");
    for (const auto type : {1, 2, 4}) {
        rejected = record; rejected.attacker_type = type;
        check(model->publish(inner, rejected, 106, 20, 17) == Publication::duplicate,
              "damage source changed the same death identity");
    }
    rejected = record; rejected.death_time = 0;
    check(model->publish(inner, rejected, 106, 20, 17) == Publication::ignored, "death without native state evidence counted");
    rejected = record; rejected.attack_type = 3;
    check(model->publish(inner, rejected, 106, 20, 17) == Publication::duplicate, "glory end repeated an emitted life");
    model->release(outer); model->release(inner);
    const auto same_life = model->capture(ctx.world, 0x30000, 3, 0x123456, 106, 20, 17);
    check(model->publish(same_life, record, 106, 20, 17) == Publication::duplicate, "released claim forgot emitted life");
    model->release(same_life);
    check(model->publish(inner, record, 106, 21, 17) == Publication::ignored, "event stamp mismatch accepted");
    check(model->publish(inner, record, 106, 20, 18) == Publication::ignored, "foreign thread accepted");
    check(model->publish(inner, record, 1200, 20, 17) == Publication::ignored, "stale player accepted");
    auto other_room = ctx; std::memset(other_room.namespace_id, 'b', 64); model->context(other_room);
    check(model->publish(inner, record, 107, 20, 17) == Publication::ignored, "room switch reused pending death");
    model->transition(2); model->context(context(2));
    check(model->publish(inner, record, 108, 20, 17) == Publication::ignored, "checkpoint replayed pending death");
    std::array<uint8_t, 0x128> bytes{};
    write<uint32_t>(bytes, 0x48, 0x123456); write<int32_t>(bytes, 0x4c, 1);
    write<uint64_t>(bytes, 0x50, 0x1234); write<uint32_t>(bytes, 0x80, 9);
    write<int32_t>(bytes, 0x84, 0); write<uint64_t>(bytes, 0x98, ctx.player);
    write<uint32_t>(bytes, 0xb8, 10); write<int32_t>(bytes, 0x38, 3);
    write<int64_t>(bytes, 0xf0, 999960); write<int32_t>(bytes, 0x124, 1);
    const auto decoded = decode_record(bytes);
    check(decoded.victim_spawn_id == record.victim_spawn_id && decoded.attacker_player == ctx.player &&
        decoded.death_time == record.death_time && decoded.attack_type == 3, "native record fixture decoded incorrectly");
    Snapshot source{}; source.pid = 101; source.process_created = 102;
    source.instance.fill(0x33); source.core.abi_version = SC_ABI_VERSION;
    std::memcpy(source.core.version, "1.1.0", 6); std::memset(source.core.build_id, 'c', 64);
    page = {}; page.size = sizeof(page); page.abi_version = SC_COMBAT_ABI_VERSION;
    page.scope.pid = source.pid; page.scope.process_created = source.process_created;
    std::memcpy(page.scope.instance_id, source.instance.data(), 16);
    page.availability = SC_NATIVE_ENABLED; model->snapshot(query, page);
    Message wire{};
    const auto size = encode_combat_response(wire, WireResult::ok, source, page);
    Snapshot decoded_source{}; sc_combat_snapshot decoded_page{}; WireResult result{};
    check(size && decode_combat_response(wire, size, result, decoded_source, decoded_page), "combat wire roundtrip failed");
    check(decoded_page.qualified_kills == 2 && !std::strcmp(decoded_page.namespace_id, query.namespace_id),
          "wire changed cumulative facts or namespace");
    if (argc >= 2 && size) {
        std::ofstream fixture(argv[1], std::ios::binary);
        fixture.write(reinterpret_cast<const char*>(wire.data()), static_cast<std::streamsize>(size));
        check(static_cast<bool>(fixture), "wire fixture write failed");
    }
    check(!decode_combat_response(wire, size - 1, result, decoded_source, decoded_page), "truncated frame accepted");
    Message request{}; const auto request_size = encode_combat_request(request, source.pid, query);
    if (argc >= 3) {
        std::ofstream fixture(argv[2], std::ios::binary);
        fixture.write(reinterpret_cast<const char*>(request.data()), static_cast<std::streamsize>(request_size));
        check(static_cast<bool>(fixture), "combat request fixture write failed");
    }
    uint16_t operation = 0; sc_combat_query decoded_query{};
    check(decode_request(request, request_size, &operation, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, &decoded_query) == WireResult::ok && operation == combat_operation &&
        !std::strcmp(decoded_query.namespace_id, query.namespace_id) && decoded_query.map_count == 1 &&
        !std::strcmp(decoded_query.maps[0], query.maps[0]), "combat selection request changed");
    check(decode_request(request, request_size - 1) == WireResult::malformed, "truncated combat query accepted");
    auto invalid_query = query; invalid_query.map_count = 2;
    std::memcpy(invalid_query.maps[1], invalid_query.maps[0], SC_CONTEXT_MAP_CAPACITY);
    check(!valid_query(invalid_query) && !encode_combat_request(request, source.pid, invalid_query),
          "duplicate map selection accepted");
    const auto ctx2 = context(2); model->registered(ctx2.world, 0x30000, 3, 0x123456);
    check(!model->confirmed_dead(ctx2.world, 0x30000, 3, 0x123456), "reused registration kept a prior death");
    const auto next = model->capture(ctx2.world, 0x30000, 3, 0x123456, 109, 20, 17);
    const auto nested_same_life = model->capture(ctx2.world, 0x30000, 3, 0x123456, 109, 20, 17);
    check(model->publish(nested_same_life, record, 110, 20, 17) == Publication::published &&
        model->publish(next, record, 110, 20, 17) == Publication::duplicate, "same-life reentry repeated death");
    model->release(nested_same_life);
    model->release(next);
    for (unsigned n = 0; n < 1000; ++n) {
        model->registered(ctx2.world, 0x30000, 3, 0x123456);
        const auto life = model->capture(ctx2.world, 0x30000, 3, 0x123456, 110, 20, 17);
        model->publish(life, record, 110, 20, 17); model->release(life);
    }
    page = {}; model->snapshot(query, page);
    check(page.qualified_kills == 1003, "unconsumed deaths were truncated");
    const auto kill = [&](Model& target, const Context& scope, int attacker_type = 0) {
        target.context(scope); target.registered(scope.world, 0x30000, 3, record.victim_spawn_id);
        auto life = target.capture(scope.world, 0x30000, 3, record.victim_spawn_id, 110, 20, 17);
        auto cause = record; cause.attacker_type = attacker_type; cause.attacker_player = 0x50000;
        const auto result = target.publish(life, cause, 110, 20, 17);
        check(target.publish(life, cause, 110, 20, 17) == Publication::duplicate,
              "a repeated death notification counted twice");
        target.release(life); return result;
    };
    auto room_b = ctx2; std::memset(room_b.namespace_id, 'b', 64);
    check(kill(*model, room_b) == Publication::published, "room B kill rejected");
    model->snapshot(query_for(room_b), page); check(page.qualified_kills == 1, "room A leaked into B");
    check(kill(*model, ctx2) == Publication::published, "return to room A rejected");
    model->snapshot(query, page); check(page.qualified_kills == 1004, "return to A reset its facts");
    auto hub = ctx2; std::memset(hub.map.bytes, 0, sizeof(hub.map.bytes));
    strcpy_s(hub.map.bytes, "game/hub"); hub.map.length = 8;
    kill(*model, hub); model->snapshot(query, page);
    check(page.qualified_kills == 1004, "unselected Hub counted");
    auto combined = query; combined.map_count = 2; strcpy_s(combined.maps[1], hub.map.bytes);
    model->snapshot(combined, page); check(page.qualified_kills == 1005, "selected map sum lost facts");
    model->transition(3); model->snapshot(query, page);
    check(page.qualified_kills == 1004, "checkpoint or unready context reset cumulative facts");
    const auto ctx3 = context(3); kill(*model, ctx3);
    model->snapshot(query, page); check(page.qualified_kills == 1005, "new respawn life was lost");
    for (const auto type : {0, 1, 2, 4}) {
        check(kill(*model, ctx3, type) == Publication::published,
              "a death from another player, infighting or the environment was lost");
    }
    page = {}; model->snapshot(query, page);
    check(page.qualified_kills == 1009 && page.observation_gaps == 0,
          "deaths from different sources changed the cumulative total");
    auto limited = std::make_unique<Model>(); limited->transition(1);
    kill(*limited, ctx);
    auto map_scope = ctx;
    for (size_t n = 1; n < counter_capacity; ++n) {
        std::snprintf(map_scope.map.bytes, sizeof(map_scope.map.bytes), "game/sp/isolated/%zu", n);
        map_scope.map.length = static_cast<uint32_t>(std::strlen(map_scope.map.bytes));
        limited->context(map_scope);
    }
    strcpy_s(map_scope.map.bytes, "game/sp/isolated/exhausted");
    map_scope.map.length = static_cast<uint32_t>(std::strlen(map_scope.map.bytes));
    check(kill(*limited, map_scope) == Publication::ignored, "counter capacity overflow was accepted");
    page = {}; limited->snapshot(query, page);
    check(page.qualified_kills == 1 && page.observation_gaps == 1, "counter exhaustion evicted facts or hid loss");
    auto bounded = std::make_unique<Model>(); bounded->transition(1); bounded->context(ctx);
    std::array<Capture, 64> active{};
    for (uint32_t n = 0; n < active.size(); ++n) {
        bounded->registered(ctx.world, 0x60000 + n * 0x100, n, 100 + n);
        active[n] = bounded->capture(ctx.world, 0x60000 + n * 0x100, n, 100 + n, 101, 20, 17);
    }
    bounded->registered(ctx.world, 0x70000, 100, 0x123456);
    const auto exhausted = bounded->capture(ctx.world, 0x70000, 100, 0x123456, 102, 20, 17);
    check(bounded->publish(exhausted, record, 103, 20, 17) == Publication::missing_registration, "claim exhaustion hidden");
    for (const auto& claim : active) bounded->release(claim);
    const auto recovered = bounded->capture(ctx.world, 0x70000, 100, 0x123456, 104, 20, 17);
    check(bounded->publish(recovered, record, 105, 20, 17) == Publication::published, "released claim capacity leaked");
    bounded->release(recovered);
    Presentation progress;
    sc_native_scope scope{}; scope.pid = source.pid; scope.process_created = source.process_created;
    std::memcpy(scope.instance_id, source.instance.data(), 16); scope.lifecycle_generation = 1;
    sc_combat_presentation_request presentation{};
    presentation.execution.expected = scope;
    presentation.execution.request_id = 42; presentation.execution.deadline_ms = 1000;
    presentation.execution.nonce[0] = 1;
    std::memset(presentation.namespace_id, 'a', 64);
    presentation.total = 350; presentation.target = 400; presentation.visible = 1;
    check(progress.submit(presentation, scope, false).status == 1, "progress rejects unadmitted room");
    check(progress.submit(presentation, scope, true).status == 0 && !progress.view(presentation.namespace_id).milestone,
          "restored total does not fabricate milestone notices");
    presentation.first_milestone = 100; presentation.milestone = 300; presentation.milestone_step = 100;
    check(progress.submit(presentation, scope, true).status == 0, "progress accepts batched milestones");
    check(progress.submit(presentation, scope, true).status == 0, "progress retries are idempotent");
    for (uint64_t notice = 100; notice <= 300; notice += 100) {
        check(progress.view(presentation.namespace_id).milestone == notice, "each batched milestone is preserved");
        progress.notice_presented(presentation.namespace_id, notice);
    }
    check(!progress.view(presentation.namespace_id).milestone, "milestones finish once");
    presentation.total = 400; presentation.target = 0; presentation.visible = 0;
    presentation.first_milestone = presentation.milestone = 400;
    check(progress.submit(presentation, scope, true).status == 0 && !progress.view(presentation.namespace_id).visible,
          "final milestone hides counter without hiding notice");
    presentation.total = 399;
    check(progress.submit(presentation, scope, true).status == 1, "stale progress cannot decrease room total");
    presentation.namespace_id[0] = 'b'; presentation.total = 3;
    presentation.first_milestone = presentation.milestone = presentation.milestone_step = 0;
    check(progress.submit(presentation, scope, true).status == 0 && !progress.view(context().namespace_id).total,
          "another room cannot see prior progress");
    presentation.target = 100; presentation.visible = 1;
    const auto presentation_size = encode_combat_presentation_request(wire, presentation);
    sc_combat_presentation_request decoded_presentation{};
    check(presentation_size == 201 && decode_request(wire, presentation_size, &operation, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, &decoded_presentation) == WireResult::ok && decoded_presentation.total == 3,
        "presentation request roundtrip preserves progress");
    check(decode_request(wire, presentation_size - 1) == WireResult::malformed,
          "truncated presentation request refused");
    const auto accepted_progress = progress.submit(presentation, scope, true);
    const auto progress_response_size = encode_combat_presentation_response(wire, WireResult::ok, source, accepted_progress);
    sc_combat_presentation_result decoded_progress{};
    check(progress_response_size && decode_combat_presentation_response(wire, progress_response_size, result,
        decoded_source, decoded_progress) && decoded_progress.total == 3 && decoded_progress.target == 100 &&
        decoded_progress.request_id == presentation.execution.request_id && decoded_progress.nonce[0] == 1,
        "presentation response preserves progress and request identity");
    check(!decode_combat_presentation_response(wire, progress_response_size - 1, result, decoded_source, decoded_progress),
          "truncated presentation response refused");
    if (!failures) std::cout << "combat registration/delivery PASS\n";
    return failures ? 1 : 0;
}
