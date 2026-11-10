#include "protocol_v2.h"
#include "inspection_server.h"
#include "native_runtime.h"
#include "pipe_io.h"
#include "fault_injection.h"
#include "campaign_menu.h"
#include "features/expanded/expanded_internal.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "wire_fixtures.h"
#include <filesystem>
#include <set>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL v2 line %d: %s (win32=%lu)\n", __LINE__, #x, GetLastError()); std::exit(1); } } while (0)
namespace sentinel::expanded::detail {
bool drain_recovery_impl(recovery::Attempt&, const recovery::Context&);
void invoke_summon_creation(RecipeCreation*, uintptr_t, const SummonSource*);
void dispatch_recipe_locked(uintptr_t, uintptr_t, uint64_t, uintptr_t, uintptr_t, bool, const std::string&, uintptr_t&);
void summon_start_detour(uintptr_t);
extern uintptr_t recovery_image;
extern uintptr_t (*recovery_stop_maintaining)(uintptr_t, uintptr_t, uint64_t, const char*);
}
namespace sentinel::recovery { void select_targets(Attempt&, const Context&); }
namespace {
using namespace sentinel;
using generated::Bytes;
unsigned native_fallback_calls = 0;

struct SoftlockFixture {
    using Words = std::vector<uintptr_t>;
    Words world = Words(0x126680 / 8), manager = Words(0x1000 / 8), worker = Words(0x110 / 8), record = Words(0x68 / 8);
    std::array<uintptr_t, 4> locator{}, table{};
    std::array<char, 96> type{};
    uintptr_t world_address = reinterpret_cast<uintptr_t>(world.data());
    uintptr_t manager_address = reinterpret_cast<uintptr_t>(manager.data());
    uintptr_t command = reinterpret_cast<uintptr_t>(worker.data());
    uint64_t stamp = native::observation_stamp();
    template<class T> static T& at(Words& words, size_t offset) { return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(words.data()) + offset); }
    SoftlockFixture() {
        using namespace expanded::detail;
        strcpy_s(type.data() + 16, type.size() - 16, ".?AVidEncounterEvent_WaitAIRemaining@@");
        recovery_image = reinterpret_cast<uintptr_t>(type.data());
        table[0] = reinterpret_cast<uintptr_t>(locator.data()); worker[0] = reinterpret_cast<uintptr_t>(&table[1]);
        at<uintptr_t>(worker, 0x40) = manager_address;
        at<uint64_t>(worker, 0x88) = 1;
        at<uintptr_t>(manager, 0xcc0) = reinterpret_cast<uintptr_t>(&command);
        at<int32_t>(manager, 0xcc8) = 1;
        at<uint8_t>(manager, 0xdb2) = at<uint8_t>(manager, 0xba0) = 1;
        at<uint32_t>(record, 8) = at<uint32_t>(record, 12) = 0x1fffffe;
        world_global = reinterpret_cast<uintptr_t>(&world_address);
        recipe_stamp = stamp;
        CHECK(recipe_state.reset("plan", "map"));
        recipe_library.plan = "plan"; recipe_library.map = "map";
        remaining_wait_started[command] = {0, worker[0], stamp, manager_address, 7};
    }
    recovery::Manager target() const {
        recovery::Manager out; out.address = manager_address; out.identifier = 7;
        out.name = "elevator6"; out.active = true; out.recipe_blocked = true;
        return out;
    }
    void drain() {
        using namespace expanded::detail;
        recovery::Context context; context.stamp = stamp; context.world = world_address;
        context.managers.push_back(target());
        recovery::Attempt attempt; attempt.stamp = stamp; attempt.targets = context.managers;
        CHECK(drain_recovery_impl(attempt, context));
        CHECK(!at<uint8_t>(manager, 0xdb3));
    }
};

void empty_combat_recovery(SoftlockFixture& f) {
    using namespace expanded::detail;
    CHECK(recipe_state.request("owned", {"elevator6", "origin", "", 1, 1, 1, 1}));
    CHECK(!recovery_remaining_complete(f.command));
    f.drain();
    CHECK(recipe_state.requests().at("owned").closed);
    CHECK(recovery_remaining_complete(f.command));
    CHECK(remaining_result(f.command) == 1);
    SoftlockFixture::at<uintptr_t>(f.worker, 0xa8) = reinterpret_cast<uintptr_t>(f.record.data());
    SoftlockFixture::at<int32_t>(f.worker, 0xb0) = 1;
    SoftlockFixture::at<uintptr_t>(f.record, 16) = f.manager_address;
    CHECK(!recovery_remaining_complete(f.command));
    SoftlockFixture::at<int32_t>(f.worker, 0xb0) = 0;
    for (const auto offset : {0xc80, 0xcf0, 0xd08}) {
        SoftlockFixture::at<uintptr_t>(f.manager, offset) = reinterpret_cast<uintptr_t>(f.record.data());
        SoftlockFixture::at<int32_t>(f.manager, offset + 8) = 1;
        CHECK(!recovery_remaining_complete(f.command));
        SoftlockFixture::at<int32_t>(f.manager, offset + 8) = 0;
    }
    SoftlockFixture::at<int32_t>(f.worker, 0xb0) = 1;
    SoftlockFixture::at<uintptr_t>(f.record, 16) = 0;
    CHECK(recovery_remaining_complete(f.command));
    pending_summon_checkpoint.emplace();
    CHECK(!recovery_remaining_complete(f.command)); pending_summon_checkpoint.reset();
    ++recipe_stamp; CHECK(!recovery_remaining_complete(f.command)); recipe_stamp = f.stamp;
    SoftlockFixture::at<uintptr_t>(f.worker, 0x40) = f.manager_address + 8;
    CHECK(!recovery_remaining_complete(f.command));
    SoftlockFixture::at<uintptr_t>(f.worker, 0x40) = f.manager_address;
    strcpy_s(f.type.data() + 16, f.type.size() - 16, "unknown_wait");
    CHECK(!recovery_remaining_complete(f.command));
    strcpy_s(f.type.data() + 16, f.type.size() - 16, ".?AVidEncounterEvent_WaitAIRemaining@@");
    CHECK(recipe_state.request("new_producer", {"elevator6", "origin", "", 1, 1, 1, 1}));
    CHECK(!recovery_remaining_complete(f.command));
    CHECK(recipe_state.cancel("new_producer"));
    CHECK(recovery_remaining_complete(f.command));
}

void recipe_failure_and_fallback(SoftlockFixture& f) {
    using namespace expanded::detail;
    CHECK(recipe_state.request("summon/test/1", {"parent", "origin", "", 1, 1, 1, 1}));
    RecipeCreation creation; creation.key = "summon/test/1";
    creation.emission = *recipe_state.reserve(creation.key);
    std::array<uintptr_t, 4> body{}; body[1] = reinterpret_cast<uintptr_t>("missing_body");
    creation.body = reinterpret_cast<uintptr_t>(body.data());
    SummonSource source;
    invoke_summon_creation(&creation, 0, &source);
    CHECK(creation.failed && !recipe_fault.load());
    CHECK(!recipe_state.requests().at(creation.key).reserved);
    const auto retry = recipe_state.reserve(creation.key);
    CHECK(retry && retry->ordinal == 0);
    CHECK(recipe_state.created(creation.key, *retry, "actor"));
    recipe_creation_failed(creation, __func__, __LINE__, "partial_mutation");
    CHECK(recipe_fault.load());
    NativeRecipe recipe; recipe.definition.origin = "fresh"; recipe.definition.family = 'i';
    recipe.definition.source = "native_source"; recipe.definition.logical_count = 1;
    native_recipes["fresh"] = recipe;
    std::array<uintptr_t, 16> native_body{};
    native_body[1] = reinterpret_cast<uintptr_t>("ai/fodder/imp");
    *reinterpret_cast<int32_t*>(&native_body[2]) = 13;
    SoftlockFixture::at<uintptr_t>(f.manager, 0xc38) = reinterpret_cast<uintptr_t>(native_body.data());
    SoftlockFixture::at<int32_t>(f.manager, 0xc40) = 1;
    CHECK(recipe_fallback_source(f.manager_address, "fresh") == f.manager_address);
    uintptr_t result = 0;
    dispatch_recipe_locked(f.manager_address, 100, 1, 0, 0, false, "fresh", result);
    CHECK(result == 101 && native_fallback_calls == 1);
    CHECK(recipe_state.request("partial", {"elevator6", "fresh", "", 1, 1, 1, 1}));
    const auto emission = recipe_state.reserve("partial"); CHECK(emission);
    CHECK(recipe_state.created("partial", *emission));
    pending_recipes[{f.manager_address, "fresh"}] = "partial";
    CHECK(!recipe_fallback_source(f.manager_address, "fresh"));
    CHECK(!recipe_state.requests().at("partial").closed);
    dispatch_recipe_locked(f.manager_address, 100, 1, 0, 0, false, "fresh", result);
    CHECK(result == 100 && native_fallback_calls == 1);
    CHECK(recipe_fallback_source(f.manager_address + 8, "fresh") == f.manager_address);
    pending_summon_checkpoint.emplace();
    CHECK(!recipe_fallback_source(f.manager_address + 8, "fresh")); pending_summon_checkpoint.reset();
}

void summon_fallback(SoftlockFixture& f) {
    using namespace expanded::detail;
    std::array<uintptr_t, 18> component{}; component[1] = f.manager_address;
    const auto address = reinterpret_cast<uintptr_t>(component.data());
    const auto before = native_fallback_calls;
    summon_start_detour(address);
    CHECK(native_fallback_calls == before + 1);
    component_summons[address].requests.push_back({"summon/test/1", "origin"});
    summon_start_detour(address);
    CHECK(native_fallback_calls == before + 1 && recipe_fault.load());
}

void population_refusals() {
    using namespace expanded;
    SpawnLimits limits{}; limits.population = 8; limits.pressure = 6; limits.kind = 3;
    limits.heavy = 4; limits.superheavy = 1; limits.strict = true;
    std::array<uint32_t, 9> active{};
    CHECK(spawn_allowed(limits, active));
    active[3] = 4; CHECK(std::string(spawn_rejection(limits, active)) == "heavy");
    active = {}; active[7] = 8; CHECK(std::string(spawn_rejection(limits, active)) == "population");
    limits.pressure = 8; CHECK(std::string(spawn_rejection(limits, active)) == "population"); limits.pressure = 6;
    active[7] = 5; active[1] = 1; CHECK(std::string(spawn_rejection(limits, active)) == "pressure");
    active = {}; active[6] = 1; limits.kind = 6;
    CHECK(std::string(spawn_rejection(limits, active)) == "superheavy");
    limits.kind = 2; CHECK(std::string(spawn_rejection(limits, active)) == "mixed_special_superheavy");
    active = {}; limits.maximum = {1, 1, 1, 1}; active[2] = 1;
    CHECK(std::string(spawn_rejection(limits, active)) == "individual_special");
    active = {}; active[4] = 1; CHECK(std::string(spawn_rejection(limits, active)) == "combined_special");
}

void recovery_cancellation_and_selection() {
    recovery::Context context; context.stamp = 9; context.map = "map";
    context.gameplay = context.supported = true;
    recovery::Manager target; target.address = 1; target.identifier = 2; target.name = "elevator6";
    target.active = target.recipe_blocked = true; context.managers = {target};
    struct Change { void (*apply)(recovery::Context&); const char* reason; };
    const Change changes[]{
        {+[](recovery::Context& c) { ++c.stamp; }, "stamp previous=9 current=10"},
        {+[](recovery::Context& c) { ++c.scope.lifecycle_generation; }, "generation previous=0 current=1"},
        {+[](recovery::Context& c) { c.map = "other"; }, "map previous=map current=other"},
        {+[](recovery::Context& c) { ++c.world; }, "world previous=0 current=1"},
        {+[](recovery::Context& c) { ++c.player; }, "player previous=0 current=1"},
        {+[](recovery::Context& c) { c.namespace_id = "other"; }, "namespace previous= current=other"},
        {+[](recovery::Context& c) { c.save = "other"; }, "save previous= current=other"},
        {+[](recovery::Context& c) { c.gameplay = false; }, "gameplay=false"},
        {+[](recovery::Context& c) { ++c.managers[0].identifier; }, "manager_identifier"},
        {+[](recovery::Context& c) { c.managers[0].name = "other"; }, "manager_name"},
        {+[](recovery::Context& c) { c.managers.clear(); }, "manager_missing"},
    };
    for (const auto& change : changes) {
        recovery::Queue queue; queue.observe(context, 100); queue.enqueue_console(9, 100);
        CHECK(queue.tick(9, 100, +[](recovery::Attempt&, const recovery::Context&) { return true; }) == std::vector<recovery::Outcome>{recovery::pending});
        auto next = context; change.apply(next); queue.observe(next, 101);
        CHECK(queue.tick(9, 101) == std::vector<recovery::Outcome>{recovery::changed});
        CHECK(queue.history().back().wait.find(change.reason) != std::string::npos);
    }
    context.managers.push_back(target); context.managers.back().address = 3;
    recovery::Attempt attempt; attempt.context = context;
    recovery::select_targets(attempt, context);
    CHECK(attempt.targets.empty());
}

void softlock_contract() {
    using namespace expanded::detail;
    const auto saved = std::make_tuple(world_global, recipe_stamp, definition_name, actor_identifier, find_entity,
        cast_maintain, cast_stagger, recovery_image, recovery_stop_maintaining, definition_database, lookup_definition,
        original_single, original_summon_start, stopped.load());
    CHECK(native_recipes.empty() && recipe_state.requests().empty());
    SoftlockFixture f;
    definition_name = +[](uintptr_t) -> const char* { return "elevator6"; };
    actor_identifier = +[](uintptr_t) -> uint32_t { return 7; };
    find_entity = +[](uintptr_t, const char*) -> uintptr_t { return 0; };
    cast_maintain = cast_stagger = +[](uintptr_t) -> uintptr_t { return 0; };
    recovery_stop_maintaining = +[](uintptr_t, uintptr_t, uint64_t, const char*) -> uintptr_t { return 0; };
    empty_combat_recovery(f);
    definition_database = +[]() -> uintptr_t { return 0; };
    lookup_definition = +[](uintptr_t, const char*, int32_t) -> uintptr_t { return 0; };
    find_entity = +[](uintptr_t world, const char*) -> uintptr_t { return *reinterpret_cast<const uintptr_t*>(world); };
    f.world[0] = f.manager_address;
    original_single = +[](uintptr_t, uintptr_t output, uint64_t, uintptr_t source, uintptr_t) -> uintptr_t {
        CHECK(source); ++native_fallback_calls; return output + 1;
    };
    original_summon_start = +[](uintptr_t) { ++native_fallback_calls; };
    stopped.store(false);
    recipe_failure_and_fallback(f);
    summon_fallback(f);
    recovery_cancellation_and_selection();
    population_refusals();
    expanded::map_freed();
    bool was_stopped = false;
    std::tie(world_global, recipe_stamp, definition_name, actor_identifier, find_entity, cast_maintain, cast_stagger,
        recovery_image, recovery_stop_maintaining, definition_database, lookup_definition, original_single, original_summon_start, was_stopped) = saved;
    stopped.store(was_stopped);
    std::puts("PASS empty combat recovery, partial recipe fault, fresh native fallback, cancellation and ambiguous selection");
}
struct Reply {
    generated::command_reply envelope{};
    v2::CommandError error{};
    generated::sc_diagnostic_result execution{};
    Bytes value;
};
Reply decode_reply(const V2Response& result) {
    CHECK(result.transport == ProbeResult::ok);
    Reply out;
    CHECK(v2::command_response(result.body, out.envelope, out.error, out.execution, out.value)); return out;
}
template<class T> Bytes encode(const T& value) {
    Bytes out; CHECK(generated::encode(value, out)); return out;
}
generated::submission submission(V2Connection& connection, const char* id) {
    const auto& scope = connection.identity(); generated::submission out{};
    out.reference.scope = {scope.pid, scope.process_created, {}, scope.lifecycle_generation};
    std::copy(std::begin(scope.instance_id), std::end(scope.instance_id), out.reference.scope.instance_id.begin());
    out.reference.namespace_id.fill('0'); out.reference.command_id = id;
    out.reference.command_type = 5; out.reference.payload_revision = 1; out.deadline_ms = 3000; return out;
}
Bytes submit_body(const generated::submission& input) {
    auto out = encode(input); const auto payload = encode(generated::diagnostic_payload{generated::diagnostic_diagnostic_payload{}});
    out.insert(out.end(), payload.begin(), payload.end()); return out;
}
Reply request(V2Connection& connection, uint16_t operation, const Bytes& bytes) {
    const auto result = connection.exchange(operation, bytes, 2000);
    if (result.transport != ProbeResult::ok) std::fprintf(stderr, "v2 operation %u transport %d error %u\n", operation, static_cast<int>(result.transport), result.win32_error);
    return decode_reply(result);
}
Reply terminal(V2Connection& connection, const generated::command_reference& reference) {
    const auto deadline = GetTickCount64() + 4000;
    for (;;) {
        const auto out = request(connection, 3, encode(reference));
        if (out.envelope.dimensions.progress == 2) return out;
        CHECK(remaining(deadline)); Sleep(1);
    }
}
void reconnect(V2Connection& connection) {
    const auto instance = current_snapshot().instance;
    CHECK(connection.connect(GetCurrentProcessId(), 2000, &instance) == ProbeResult::ok);
}
void dropped(V2Connection& connection, void (*resume)(), void (*park)(), std::atomic<uint64_t>& executions) {
    const auto original = submission(connection, "dropped");
    generated::command_reference reference;
    const auto payload = encode(generated::diagnostic_payload{generated::diagnostic_diagnostic_payload{}});
    CHECK(v2_reference(original, payload, reference));
    const auto before = executions.load();
    testing::drop_reply(2);
    CHECK(connection.exchange(2, submit_body(original), 2000).transport != ProbeResult::ok);
    reconnect(connection);
    auto out = request(connection, 3, encode(reference));
    CHECK(out.execution.state == SC_DIAGNOSTIC_QUEUED && out.error == v2::CommandError::none);
    const auto duplicate = request(connection, 2, submit_body(original));
    CHECK(duplicate.execution.admitted_at_ms == out.execution.admitted_at_ms);
    auto altered = original; ++altered.deadline_ms;
    CHECK(request(connection, 2, submit_body(altered)).error == v2::CommandError::duplicate_mismatch);
    auto wrong = reference; wrong.namespace_id[0] = 'a';
    CHECK(request(connection, 3, encode(wrong)).error == v2::CommandError::duplicate_mismatch);
    CHECK(request(connection, 5, encode(generated::ack{reference, {}})).error == v2::CommandError::not_terminal);
    resume(); out = terminal(connection, reference); park();
    CHECK(executions.load() == before + 1 && out.envelope.dimensions.execution == 1);
    testing::drop_reply(3);
    CHECK(connection.exchange(3, encode(reference), 2000).transport != ProbeResult::ok);
    reconnect(connection); out = request(connection, 3, encode(reference));
    CHECK(out.execution.state == SC_DIAGNOSTIC_EXECUTED && executions.load() == before + 1);
    const auto ack = generated::ack{reference, out.envelope.dimensions.result_token};
    auto wrong_ack = ack; ++wrong_ack.result_token[0];
    CHECK(request(connection, 5, encode(wrong_ack)).error == v2::CommandError::token_mismatch);
    testing::drop_reply(5);
    CHECK(connection.exchange(5, encode(ack), 2000).transport != ProbeResult::ok);
    reconnect(connection); out = request(connection, 3, encode(reference));
    CHECK(out.envelope.dimensions.retention == 1 && out.execution.state == SC_DIAGNOSTIC_EXECUTED);
    CHECK(request(connection, 5, encode(ack)).error == v2::CommandError::none);
    CHECK(request(connection, 2, submit_body(original)).envelope.dimensions.retention == 1);
    CHECK(executions.load() == before + 1);
}
void cancellation(V2Connection& connection, void (*resume)(), void (*park)(), HANDLE entered, HANDLE release,
                  std::atomic<unsigned>& stage, std::atomic<uint64_t>& executions) {
    auto input = submission(connection, "cancel-before");
    auto admitted = request(connection, 2, submit_body(input));
    const auto before = executions.load();
    auto cancelled = request(connection, 4, encode(admitted.envelope.reference));
    CHECK(cancelled.execution.state == SC_DIAGNOSTIC_CANCELLED && cancelled.envelope.dimensions.execution == 0 && cancelled.envelope.dimensions.effect == 0);
    input.reference.command_id = "cancel-after";
    admitted = request(connection, 2, submit_body(input));
    CHECK(ResetEvent(entered)); CHECK(ResetEvent(release)); stage.store(2); resume();
    CHECK(WaitForSingleObject(entered, 4000) == WAIT_OBJECT_0);
    cancelled = request(connection, 4, encode(admitted.envelope.reference));
    CHECK(cancelled.execution.state == SC_DIAGNOSTIC_CLAIMED && cancelled.execution.cancel_requested);
    CHECK(request(connection, 5, encode(generated::ack{admitted.envelope.reference, {}})).error == v2::CommandError::not_terminal);
    CHECK(SetEvent(release));
    const auto done = terminal(connection, admitted.envelope.reference); park();
    CHECK(done.execution.state == SC_DIAGNOSTIC_EXECUTED && done.envelope.dimensions.execution == 1 && executions.load() == before + 1);
}
void admission_refusals(V2Connection& connection, std::atomic<uint64_t>& executions) {
    const auto before = executions.load();
    auto input = submission(connection, "not-admitted"); input.reference.command_type = 18; input.reference.namespace_id.fill('a');
    auto body = encode(input); const auto payload = encode(generated::weapon_points_payload{generated::weapon_points_grant_payload{3,0}});
    body.insert(body.end(),payload.begin(),payload.end());
    const auto refused = request(connection,2,body);
    CHECK(refused.error == v2::CommandError::none && refused.execution.state == SC_DIAGNOSTIC_REJECTED && refused.execution.reason == SC_NATIVE_BINDING_FAILED);
    CHECK(refused.envelope.dimensions.progress == 2 && refused.envelope.dimensions.execution == 0 && refused.envelope.dimensions.effect == 0);
    std::array<Reply, SC_DIAGNOSTIC_CAPACITY> held;
    for (size_t i = 0; i < held.size(); ++i) {
        const auto id = "held-" + std::to_string(i);
        held[i] = request(connection,2,submit_body(submission(connection,id.c_str())));
        CHECK(held[i].execution.state == SC_DIAGNOSTIC_QUEUED);
    }
    input = submission(connection,"queue-full");
    const auto full = request(connection,2,submit_body(input));
    CHECK(full.error == v2::CommandError::none && full.execution.state == SC_DIAGNOSTIC_REJECTED && full.execution.reason == SC_NATIVE_QUEUE_FULL);
    CHECK(full.envelope.dimensions.progress == 2 && full.envelope.dimensions.execution == 0 && full.envelope.dimensions.effect == 0);
    for (const auto& queued : held) {
        const auto cancelled = request(connection,4,encode(queued.envelope.reference));
        CHECK(cancelled.execution.state == SC_DIAGNOSTIC_CANCELLED);
        CHECK(request(connection,5,encode(generated::ack{queued.envelope.reference,cancelled.envelope.dimensions.result_token})).error == v2::CommandError::none);
    }
    CHECK(request(connection,5,encode(generated::ack{full.envelope.reference,full.envelope.dimensions.result_token})).error == v2::CommandError::none);
    CHECK(request(connection,2,submit_body(input)).execution.reason == SC_NATIVE_QUEUE_FULL);
    CHECK(request(connection,3,encode(full.envelope.reference)).envelope.dimensions.execution == 0);
    input.reference.command_id = "queue-full-replacement";
    const auto replacement = request(connection,2,submit_body(input));
    CHECK(replacement.execution.state == SC_DIAGNOSTIC_QUEUED);
    CHECK(request(connection,4,encode(replacement.envelope.reference)).execution.state == SC_DIAGNOSTIC_CANCELLED);
    CHECK(executions.load() == before);
}
HANDLE raw_pipe() {
    const auto deadline = GetTickCount64() + 2000; const auto name = pipe_name_v2(GetCurrentProcessId());
    for (;;) {
        const auto pipe = CreateFileW(name.c_str(), client_access, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) return pipe;
        CHECK(GetLastError() == ERROR_PIPE_BUSY && remaining(deadline));
        CHECK(WaitNamedPipeW(name.c_str(), remaining(deadline)));
    }
}
generated::wire_header_2 read_frame(HANDLE pipe, Bytes& body) {
    Bytes bytes(32); generated::wire_header_2 header;
    CHECK(transfer_exact(pipe, false, bytes.data(), 32, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    CHECK(v2::header(bytes, header, true)); body.resize(header.payload_length);
    CHECK(transfer_exact(pipe, false, body.data(), header.payload_length, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS); return header;
}
void refused_v1() {
    Handle pipe(raw_pipe());
    const auto bytes = wire_fixtures::read(std::filesystem::path(SC_WIRE_V2_FIXTURE_FILE).parent_path() / "v1_refused.json").at("inspect_request").bytes;
    CHECK(transfer_exact(pipe.value, true, const_cast<uint8_t*>(bytes.data()), static_cast<DWORD>(bytes.size()), nullptr, GetTickCount64()+1000) == ERROR_SUCCESS);
    uint8_t byte = 0; DWORD count = 0;
    const auto error = transfer(pipe.value, false, &byte, 1, count, nullptr, 2000);
    CHECK(error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED);
}
void framing() {
    Handle pipe(raw_pipe()); Bytes frame, body;
    generated::wire_header_2 header{generated::wire_magic, 2, 6, 0, 0, 7, 0, 0};
    CHECK(v2::frame(header, {}, frame));
    for (auto& byte : frame) CHECK(transfer_exact(pipe.value, true, &byte, 1, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    CHECK(read_frame(pipe.value, body).correlation_id == 7);
    auto combined = frame; header.correlation_id = 8; CHECK(v2::frame(header, {}, frame));
    combined.insert(combined.end(), frame.begin(), frame.end());
    CHECK(transfer_exact(pipe.value, true, combined.data(), static_cast<DWORD>(combined.size()), nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    CHECK(read_frame(pipe.value, body).correlation_id == 7); CHECK(read_frame(pipe.value, body).correlation_id == 8);
    frame[8] = 0xe1; frame[9] = 0x3f;
    CHECK(transfer_exact(pipe.value, true, frame.data(), 32, nullptr, GetTickCount64() + 2000) == ERROR_SUCCESS);
    uint8_t byte = 0; DWORD count = 0;
    const auto error = transfer(pipe.value, false, &byte, 1, count, nullptr, 1500);
    CHECK(error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED);
}
void identity(V2Connection& connection) {
    auto expected = current_snapshot().instance; ++expected[0];
    V2Connection wrong; CHECK(wrong.connect(GetCurrentProcessId(), 2000, &expected) == ProbeResult::process_mismatch);
    auto input = submission(connection, "wrong-instance"); ++input.reference.scope.instance_id[0];
    auto out = request(connection, 2, submit_body(input));
    CHECK(out.error == v2::CommandError::effect_unknown && out.envelope.actual_instance_id == current_snapshot().instance);
    input = submission(connection, "wrong-pid"); ++input.reference.scope.pid;
    CHECK(request(connection, 2, submit_body(input)).error == v2::CommandError::effect_unknown);
    input = submission(connection, "unsupported"); input.reference.payload_revision = 99;
    CHECK(request(connection, 2, submit_body(input)).error == v2::CommandError::unsupported_revision);
    reconnect(connection);
    input = submission(connection, "missing"); generated::command_reference reference;
    CHECK(v2_reference(input, encode(generated::diagnostic_payload{generated::diagnostic_diagnostic_payload{}}), reference));
    out = request(connection, 3, encode(reference));
    CHECK(out.error == v2::CommandError::not_found && out.envelope.dimensions.effect == 4);
}
void observations(V2Connection& connection) {
    const auto s = submission(connection, "unused");
    generated::read_envelope input{s.reference.scope, s.reference.namespace_id, 1, 1, {}};
    auto body = encode(input); const auto payload = encode(generated::inspect_payload{generated::inspect_inspect_payload{}});
    body.insert(body.end(), payload.begin(), payload.end());
    const auto result = connection.exchange(1, body, 2000);
    CHECK(result.transport == ProbeResult::ok && result.wire == WireResult::ok);
    generated::sc_status status; CHECK(generated::decode(Bytes(result.body.begin() + 36, result.body.end()), status));
    CHECK(status.abi_version == SC_ABI_VERSION);
    input.command_type = 18; body = encode(input);
    const auto grant = encode(generated::weapon_points_payload{generated::weapon_points_grant_payload{3, 0}});
    body.insert(body.end(), grant.begin(), grant.end());
    const auto rejected = connection.exchange(1, body, 2000);
    CHECK(rejected.transport == ProbeResult::ok && rejected.wire == WireResult::unsupported_operation);
}

void transient_model() {
    native::Diagnostics queue; ::sc_weapon_points_request payload{};
    std::memset(payload.namespace_id, 'a', 64);
    ::sc_diagnostic_request execution{}; execution.request_id = 1; execution.nonce[0] = 1; execution.deadline_ms = 1000;
    auto* slot = queue.transient(execution, payload, 0, 100); CHECK(slot);
    CHECK(queue.identity_count() == 0 && queue.claim(101) == slot);
    auto done = slot->result; done.state = SC_DIAGNOSTIC_EXECUTED; done.completed_at_ms = 102;
    native::Diagnostics::finish(*slot, done); native::Diagnostics::Reply reply;
    CHECK(queue.collect_transient(*slot, reply, 103));
    CHECK(reply.execution.state == SC_DIAGNOSTIC_EXECUTED && queue.identity_count() == 0 && !queue.claim(104));
    CHECK(std::get<::sc_weapon_points_result>(reply.value).execution.state == SC_DIAGNOSTIC_EXECUTED);
}
template<class Native, class Payload> Native converted(V2Connection& connection, uint16_t id, generated::Operation operation, const Payload& payload, uint16_t revision = 1) {
    auto envelope = submission(connection, "adapter"); envelope.reference.command_type = id;
    envelope.reference.payload_revision = revision;
    const auto* descriptor = registry::descriptor(id, revision); CHECK(descriptor);
    generated::NativeRequest native;
    CHECK(v2::native_payload(*descriptor, encode(payload), envelope, native, operation));
    return std::get<Native>(native);
}
void automap_revisions(V2Connection& c) {
    const auto submit = generated::Operation::submit;
    generated::automap_publish_payload legacy{1, 7, {1}};
    const auto legacy_request = converted<::sc_automap_request>(c, 45, submit, generated::automap_payload_revision1{legacy});
    CHECK(legacy_request.checked_locations[0] == 1 && legacy_request.checked_locations[78] == 0);
    generated::automap_publish_arena_payload arenas{}; arenas.known = 1; arenas.revision = 8;
    const auto arena_bit = 7774901u - SC_AUTOMAP_LOCATION_BASE;
    arenas.checked_locations[arena_bit/64] = uint64_t{1} << (arena_bit%64);
    const auto arena_request = converted<::sc_automap_request>(c, 45, submit, generated::automap_payload{arenas}, 2);
    CHECK(arena_request.revision == 8 && arena_request.checked_locations[arena_bit/64] == arenas.checked_locations[arena_bit/64]);
}

Bytes fixture(const char* name) {
    return wire_fixtures::read(SC_WIRE_V2_FIXTURE_FILE).at(name).bytes;
}
generated::NativeCampaign campaign_fixture(const char* name, uint16_t revision = 5, char namespace_first = 'a') {
    const auto bytes = fixture(name); generated::submission input; Bytes payload;
    CHECK(v2::split_submission(Bytes(bytes.begin() + 32, bytes.end()), input, payload));
    input.reference.payload_revision = revision;
    input.reference.namespace_id[0] = namespace_first;
    if (revision < 4) {
        if (input.reference.command_type == 23) payload.resize(12);
        else if (revision == 1) payload.resize(320);
        else if (revision == 2) payload.resize(612);
        else payload.erase(payload.begin() + 612, payload.begin() + 616);
    }
    const auto* descriptor = registry::descriptor(input.reference.command_type, revision); CHECK(descriptor);
    generated::NativeRequest native;
    CHECK(v2::native_payload(*descriptor, payload, input, native, generated::Operation::submit));
    return std::get<generated::NativeCampaign>(native);
}
void campaign_upload() {
    campaign_menu::Menu menu;
    const auto hub = campaign_fixture("campaign_row_unknown");
    const auto mission = campaign_fixture("campaign_row_known");
    const auto commit = campaign_fixture("campaign_commit_selection");
    CHECK(commit.request.revision == 7 && commit.request.count == 2 && commit.request.row.id == 2);
    CHECK(menu.request(campaign_row_operation, hub.request, true, &hub.summary, &hub.rewards).status == 0);
    const auto incomplete = menu.request(campaign_commit_operation, commit.request, true);
    CHECK(incomplete.status == SC_CAMPAIGN_REFUSED && incomplete.reason == SC_CAMPAIGN_INCOMPLETE);
    CHECK(menu.projection().count == 0);
    CHECK(menu.request(campaign_row_operation, mission.request, true, &mission.summary, &mission.rewards).status == 0);
    CHECK(menu.request(campaign_commit_operation, commit.request, true).committed_revision == 7);
    const auto wrong = campaign_fixture("campaign_commit_selection", 5, 'b');
    CHECK(wrong.request.namespace_id[0] == 'b');
    const bool same_namespace = !std::strcmp(wrong.request.namespace_id, hub.request.namespace_id);
    const auto refused = menu.request(campaign_commit_operation, wrong.request, same_namespace);
    CHECK(refused.status == SC_CAMPAIGN_REFUSED && refused.reason == SC_CAMPAIGN_SCOPE);
    CHECK(refused.committed_revision == 0 && refused.selected_id == 0);
    const auto shown = menu.projection();
    CHECK(shown.revision == 7 && shown.count == 2 && shown.focus_id == 2 && menu.focus_id() == 2);
    CHECK(shown.rewards_known[0] == 0 && shown.rewards[0].empty());
    CHECK(shown.rewards_known[1] == 1 && shown.rewards[1].size() == 1);
    CHECK(shown.rewards[1][0].location_id == 7770001);
    menu.selected(1);
    CHECK(menu.request(campaign_commit_operation, commit.request, true).status == 0 && menu.focus_id() == 1);
    for (uint16_t revision = 1; revision <= 3; ++revision) {
        const auto old = campaign_fixture("campaign_row_known", revision);
        CHECK(old.request.row.id == 2 && old.request.count == 2);
        CHECK(old.rewards.known == (revision == 3 ? 1u : 0u));
        CHECK(campaign_fixture("campaign_commit_selection", revision).request.revision == 7);
    }
}
void fixture_requests() {
    const auto expected = fixture("queued_query");
    generated::command_reference reference;
    CHECK(generated::decode(Bytes(expected.begin() + 32, expected.end()), reference));
    for (const auto name : {"submit", "matching_duplicate", "conflicting_duplicate", "wrong_namespace", "wrong_binding", "unsupported_revision"}) {
        const auto bytes = fixture(name); generated::submission input; Bytes payload;
        CHECK(v2::split_submission(Bytes(bytes.begin() + 32, bytes.end()), input, payload));
        const auto prefix = encode(input); Bytes actual = prefix; actual.insert(actual.end(), payload.begin(), payload.end());
        CHECK(Bytes(bytes.begin() + 32, bytes.end()) == actual);
        generated::weapon_points_payload decoded; CHECK(generated::decode(payload, decoded));
        CHECK(encode(decoded) == payload);
        if (std::string(name) == "submit") {
            generated::command_reference computed; CHECK(v2_reference(input, payload, computed));
            CHECK(computed.fingerprint == reference.fingerprint);
        }
    }
    for (const auto name : {"queued_query", "terminal_query", "cancel_before_entry", "cancel_after_entry"}) {
        const auto bytes = fixture(name); Bytes body(bytes.begin() + 32, bytes.end());
        generated::command_reference ref; CHECK(generated::decode(body, ref) && encode(ref) == body);
    }
    for (const auto name : {"ack", "repeated_ack"}) {
        const auto bytes = fixture(name); Bytes body(bytes.begin() + 32, bytes.end());
        generated::ack ack; CHECK(generated::decode(body, ack) && encode(ack) == body);
    }
}
void command_families() {
    const auto path = std::filesystem::path(SC_WIRE_V2_FIXTURE_FILE).parent_path() / "commands.json";
    std::set<unsigned> covered;
    for (const auto& [name, fixture] : wire_fixtures::read(path)) {
        unsigned id, revision, tag, submit;
        CHECK(sscanf_s(name.c_str(), "%u_%u_%u_%u", &id, &revision, &tag, &submit) == 4);
        const auto* descriptor = registry::descriptor(static_cast<uint16_t>(id), static_cast<uint16_t>(revision));
        CHECK(descriptor);
        generated::Payload payload;
        Bytes encoded;
        CHECK(descriptor->decode_payload(fixture.bytes, payload));
        CHECK(descriptor->encode_payload(payload, encoded) && encoded == fixture.bytes);
        generated::submission envelope{};
        envelope.reference.command_type = static_cast<uint16_t>(id);
        envelope.reference.payload_revision = static_cast<uint16_t>(revision);
        envelope.reference.namespace_id.fill('a');
        generated::NativeRequest native;
        if (!v2::native_payload(*descriptor, encoded, envelope, native,
                               submit ? generated::Operation::submit : generated::Operation::read)) {
            std::fprintf(stderr, "FAIL command family %s\n", name.c_str());
            CHECK(false);
        }
        if (id == 23) {
            const auto& commit = std::get<generated::NativeCampaign>(native).request;
            CHECK(revision == 5 && commit.revision == 7 && commit.count == 27 && commit.row.id == 7776006);
        }
        covered.insert(id);
    }
    for (size_t i = 0; i < generated::descriptor_count; ++i)
        CHECK(covered.count(generated::descriptors[i].id) == 1);
}
void fixture_responses() {
    struct Expected { const char* name; uint32_t progress, execution, effect, retention, error; };
    const Expected cases[] = {{"queued_result",0,0,0,0,0}, {"terminal_result",2,1,1,0,0},
        {"cancelled_before_entry",2,0,0,0,0}, {"cancel_requested_after_entry",1,1,4,0,0},
        {"acked_result",2,1,1,1,0}, {"repeated_ack_result",2,1,1,1,0}, {"pruned_result",2,1,1,1,0},
        {"effect_unknown",3,2,4,2,8}, {"conflicting_duplicate_result",3,0,0,0,1}};
    for (const auto& expected : cases) {
        const auto bytes = fixture(expected.name); Bytes body(bytes.begin() + 32, bytes.end());
        generated::command_reply reply; generated::sc_diagnostic_result execution; v2::CommandError error; Bytes value;
        CHECK(v2::command_response(body, reply, error, execution, value));
        const auto& d = reply.dimensions;
        CHECK(d.progress == expected.progress && d.execution == expected.execution && d.effect == expected.effect);
        CHECK(d.retention == expected.retention && static_cast<uint32_t>(error) == expected.error);
        auto actual = encode(generated::command_response{reply, static_cast<uint32_t>(error), execution});
        if (!value.empty()) {
            generated::sc_weapon_points_result points; CHECK(generated::decode(value, points));
            CHECK(points.balance_after == 3 && encode(points) == value);
        }
        actual.insert(actual.end(), value.begin(), value.end()); CHECK(actual == body);
    }
    const auto bytes = fixture("hello_result"); generated::hello_result hello;
    const Bytes body(bytes.begin() + 32, bytes.end()); CHECK(generated::decode(body, hello));
    CHECK(hello.max_frame == 16384 && hello.supported.size() == 3 && encode(hello) == body);
}

}
void protocol_v2_contract(const wchar_t* mode, void (*park)(), void (*resume)(), HANDLE entered, HANDLE release,
                          std::atomic<unsigned>& stage, std::atomic<uint64_t>& executions) {
    using namespace sentinel;
    if (std::wcscmp(mode, L"v2")) return;
    fixture_requests(); fixture_responses(); command_families(); transient_model(); campaign_upload();
    park(); softlock_contract(); V2Connection connection; reconnect(connection);
    const auto hello = connection.exchange(6, {}, 2000); generated::hello_result supported;
    CHECK(hello.transport == ProbeResult::ok && generated::decode(hello.body, supported));
    CHECK(supported.max_frame == 16384 && supported.execution_slots == 8 && supported.full_result_ms == 30000 && supported.identity_capacity == 65536);
    CHECK(supported.supported.size() == generated::descriptor_count);
    automap_revisions(connection); refused_v1(); framing(); identity(connection); observations(connection); reconnect(connection);
    admission_refusals(connection, executions);
    dropped(connection, resume, park, executions);
    cancellation(connection, resume, park, entered, release, stage, executions);
    connection.close(); resume();
    std::puts("PASS v2 framing, identity, read safety, duplicate/cancel/ack, dropped admission/result/ack with one native entry");
}
