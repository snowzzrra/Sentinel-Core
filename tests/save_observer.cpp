#include "save_observer.h"
#include "protocol.h"
#include <map>
#include <vector>
#include <functional>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstddef>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL save observer line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
using namespace sentinel;
static_assert(sizeof(sc_status) == 136 && SC_ABI_VERSION == 1, "Existing C ABI stays exact");
static_assert(sizeof(sc_save_field) == 24 && sizeof(sc_save_snapshot) == 368 &&
    offsetof(sc_save_snapshot, fields) == 80, "Save C ABI 1 layout");
struct Fake final : engine::Memory {
    std::map<uintptr_t, std::vector<uint8_t>> blocks;
    std::vector<uintptr_t> reads;
    std::function<void(uintptr_t)> before;
    uintptr_t reject = 0;
    uint32_t failure = SC_REASON_PARTIAL_READ;
    template<class T> void put(uintptr_t address, const T& value) {
        auto& bytes = blocks[address]; bytes.resize(sizeof(value)); std::memcpy(bytes.data(), &value, sizeof(value));
    }
    engine::ReadResult copy(uintptr_t address, void* out, size_t size) override {
        reads.push_back(address); if (before) before(address);
        if (address == reject) return {failure, ERROR_PARTIAL_COPY};
        for (const auto& b : blocks) if (address >= b.first && address - b.first <= b.second.size() &&
            size <= b.second.size() - (address - b.first)) {
            std::memcpy(out, b.second.data() + (address - b.first), size); return {};
        }
        return {SC_REASON_READ_FAILED, ERROR_NOACCESS};
    }
};
int64_t counter_value = 0, counter_delta = 1000;
uint64_t uptime_value = 1000;
bool counter(int64_t& value) { value = counter_value; counter_value += counter_delta; return true; }
uint64_t uptime() { const auto value = uptime_value; uptime_value += 16; return value; }
const context::Clock clock_fixture{counter, uptime, 1000000000};
struct Fixture {
    Fake memory;
    engine::Binding binding;
    const uintptr_t root = 0x1445ea6f0ULL, manager = 0x200000000ULL, control = 0x300000000ULL,
        provider = 0x300001000ULL, job = 0x400000000ULL;
    Fixture() {
        binding.metadata.pe_reason = binding.metadata.root_locator_reason = SC_REASON_NONE;
        binding.metadata.profile = SC_PROFILE_STEAM_20260818;
        binding.image.base = 0x140000000ULL; binding.root = root;
        memory.put(root, uintptr_t{0x142aaa730ULL}); state(SC_GAME_IN_GAME, 1);
        memory.put(root + 0xb8, uint8_t{0}); memory.put(root + 0x9ca8, int32_t{0});
        memory.put(root + 0x9b38, manager);
        memory.put(manager, control); memory.put(control + 8, provider);
        memory.put(provider, uintptr_t{0x142e90658ULL});
        memory.put(manager + 0x60, uintptr_t{0}); memory.put(manager + 0x58, uintptr_t{0}); memory.put(manager + 0x50, uintptr_t{0});
    }
    void state(uint32_t value, uint32_t changed) { const uint32_t pair[]{value, changed}; memory.put(root + 0x44, pair); }
    sc_save_snapshot sample(HANDLE stop = nullptr) { return save::sample(memory, binding, 7, stop, &clock_fixture); }
};
void unresolved(const sc_save_snapshot& s) {
    CHECK(!s.mutation_available && s.mutation_reason == SC_SAVE_NATIVE_NAMESPACE_ROUTE_UNPROVEN);
    for (size_t i = SC_SAVE_SELECTED_SLOT; i < SC_SAVE_FIELD_COUNT; ++i)
        CHECK(s.fields[i].validity == SC_OBSERVATION_UNKNOWN && s.fields[i].value == 0 && s.fields[i].reason != SC_REASON_NONE);
}
void suppressed(const sc_save_snapshot& s, uint32_t reason) {
    CHECK(s.sample_reason == reason);
    for (size_t i = 0; i < SC_SAVE_SELECTED_SLOT; ++i)
        CHECK(s.fields[i].validity == SC_OBSERVATION_UNKNOWN && s.fields[i].value == 0 && s.fields[i].reason == reason);
    unresolved(s);
}
int main() {
    // Actual DLL export/size/version rejection is exercised by inspection tests.
    { Fake m; engine::Binding b; suppressed(save::sample(m, b, 1), SC_REASON_PROFILE_UNRECOGNIZED); CHECK(m.reads.empty()); }
    Fixture f; auto s = f.sample();
    CHECK(s.sample_reason == SC_REASON_NONE && s.sequence == 7 && s.layout_revision == 1 && s.duration_ms == 16);
    CHECK(s.fields[SC_SAVE_PROVIDER].value == SC_SAVE_PROVIDER_STEAM);
    CHECK(s.fields[SC_SAVE_MANAGER].value == 1 && s.fields[SC_SAVE_QUEUED_REQUESTS].value == 0);
    for (size_t i = SC_SAVE_JOB_WITNESS; i <= SC_SAVE_REQUEST50_WITNESS; ++i)
        CHECK(s.fields[i].validity == SC_OBSERVATION_OBSERVED && s.fields[i].value == 0);
    unresolved(s); // Empty jobs/count is deliberately not a native completion result.
    for (const auto& provider : std::array<std::pair<uintptr_t, uint32_t>, 3>{{
        {0x142e87db0ULL, SC_SAVE_PROVIDER_LOCAL_ENCRYPTED}, {0x142e88198ULL, SC_SAVE_PROVIDER_LOCAL}, {0x200012000ULL, SC_SAVE_PROVIDER_FOREIGN}}}) {
        Fixture t; t.memory.put(t.provider, provider.first); const auto result = t.sample();
        CHECK(result.fields[SC_SAVE_PROVIDER].validity == SC_OBSERVATION_OBSERVED && result.fields[SC_SAVE_PROVIDER].value == provider.second);
        unresolved(result);
    }
    { Fixture t; t.memory.put(t.root + 0x9b38, uintptr_t{0}); const auto result = t.sample();
      CHECK(result.fields[SC_SAVE_MANAGER].validity == SC_OBSERVATION_OBSERVED && !result.fields[SC_SAVE_MANAGER].value);
      CHECK(result.fields[SC_SAVE_PROVIDER].reason == SC_REASON_PARENT_NULL && result.fields[SC_SAVE_JOB_WITNESS].reason == SC_REASON_PARENT_NULL); }
    { Fixture t; t.memory.put(t.manager, uintptr_t{0}); CHECK(t.sample().fields[SC_SAVE_PROVIDER].reason == SC_REASON_PARENT_NULL); }
    { Fixture t; t.memory.put(t.control + 8, uintptr_t{0}); CHECK(t.sample().fields[SC_SAVE_PROVIDER].reason == SC_REASON_PARENT_NULL); }
    // Exact native-owned chains including the extra +58 nesting.
    { Fixture t; t.memory.put(t.manager + 0x60, t.job); t.memory.put(t.job + 8, t.job + 0x1000);
      t.memory.put(t.manager + 0x58, t.job + 0x2000); t.memory.put(t.job + 0x2008, t.job + 0x3000); t.memory.put(t.job + 0x3008, t.job + 0x4000);
      t.memory.put(t.manager + 0x50, t.job + 0x5000); t.memory.put(t.job + 0x5008, t.job + 0x6000);
      const auto result = t.sample(); for (size_t i = SC_SAVE_JOB_WITNESS; i <= SC_SAVE_REQUEST50_WITNESS; ++i) CHECK(result.fields[i].value == 1);
      unresolved(result); }
    { Fixture t; t.memory.put(t.manager + 0x58, t.job); t.memory.put(t.job + 8, t.job + 0x1000); t.memory.put(t.job + 0x1008, uintptr_t{0});
      CHECK(t.sample().fields[SC_SAVE_REQUEST58_WITNESS].value == 0); }
    for (const auto offset : {uintptr_t{0}, uintptr_t{0x44}, uintptr_t{0xb8}, uintptr_t{0x9b38}, uintptr_t{0x9ca8}}) {
        Fixture t; t.memory.reject = t.root + offset; const auto result = t.sample(); suppressed(result, SC_REASON_PARTIAL_READ);
        CHECK(result.fields[SC_SAVE_ROOT].win32_error == ERROR_PARTIAL_COPY);
    }
    for (const auto target : {f.manager, f.control + 8, f.provider, f.manager + 0x60, f.manager + 0x58, f.manager + 0x50}) {
        Fixture t; t.memory.reject = target; suppressed(t.sample(), SC_REASON_PARTIAL_READ);
    }
    { Fixture t; t.memory.blocks.erase(t.control + 8); suppressed(t.sample(), SC_REASON_READ_FAILED); }
    { Fixture t; t.memory.put(t.root + 0x9ca8, int32_t{-1}); suppressed(t.sample(), SC_REASON_INVALID_VALUE); }
    { Fixture t; t.memory.put(t.root + 0x9b38, uintptr_t{UINTPTR_MAX}); suppressed(t.sample(), SC_REASON_OUT_OF_RANGE); }
    { Fixture t; t.state(SC_GAME_LOADING, 2); suppressed(t.sample(), SC_REASON_TRANSITION); }
    { Fixture t; t.state(SC_GAME_MAIN_MENU, 2); CHECK(t.sample().sample_reason == SC_REASON_NONE); }
    { Fixture t; t.memory.put(t.root + 0xb8, uint8_t{1}); suppressed(t.sample(), SC_REASON_TRANSITION); }
    { Fixture t; t.memory.put(t.root + 0xb8, uint8_t{2}); suppressed(t.sample(), SC_REASON_INVALID_VALUE); }
    { Fixture t; t.memory.put(t.root, uintptr_t{0x142e90658ULL}); suppressed(t.sample(), SC_REASON_INVALID_VALUE); }
    // A second stable-value frame does not rescue a changed owner/count/state.
    for (unsigned which = 0; which < 5; ++which) {
        Fixture t; unsigned visits = 0;
        t.memory.before = [&](uintptr_t address) {
            if (address != t.root || ++visits != 2) return;
            if (which == 0) t.memory.put(t.root + 0x9ca8, int32_t{1});
            if (which == 1) t.memory.put(t.root + 0x9b38, uintptr_t{0});
            if (which == 2) t.state(SC_GAME_IN_GAME, 2);
            if (which == 3) { t.memory.put(t.manager + 0x60, t.job); t.memory.put(t.job + 8, uintptr_t{0}); }
            if (which == 4) t.memory.put(t.provider, uintptr_t{0x142e88198ULL});
        };
        suppressed(t.sample(), SC_REASON_TRANSITION);
    }
    { Fixture t; t.memory.put(t.root + 0x9ca8, int32_t{1}); unresolved(t.sample());
      t.memory.put(t.root + 0x9ca8, int32_t{0}); unresolved(t.sample()); }
    { Fixture t; HANDLE stop = CreateEventW(nullptr, TRUE, TRUE, nullptr); CHECK(stop);
      suppressed(t.sample(stop), SC_REASON_CANCELLED); CHECK(t.memory.reads.empty()); CloseHandle(stop); }
    counter_delta = 50000001; suppressed(f.sample(), SC_REASON_BUDGET); counter_delta = 1000;
    s.sampled_at_ms = 100; suppressed(save::freshness(s, 1101), SC_REASON_STALE); suppressed(save::freshness(s, 99), SC_REASON_STALE);
    CHECK(save::freshness(s, 1100).fields[SC_SAVE_PROVIDER].validity == SC_OBSERVATION_OBSERVED);

    Snapshot host{}; host.pid = 42; host.process_created = 123; host.instance[0] = 9;
    host.core.abi_version = SC_ABI_VERSION; strcpy_s(host.core.version, "0.6.0"); strcpy_s(host.core.build_id, "fixture");
    Message wire{}; Snapshot decoded{}; sc_save_snapshot output{}; WireResult result{};
    auto size = encode_save_response(wire, WireResult::ok, host, s);
    CHECK(size < max_message && decode_save_response(wire, size, result, decoded, output));
    CHECK(output.pid == 42 && output.process_created == 123 && output.instance[0] == 9);
    CHECK(output.fields[SC_SAVE_PROVIDER].value == SC_SAVE_PROVIDER_STEAM); unresolved(output);
    for (size_t n = 0; n < size; ++n) CHECK(!decode_save_response(wire, n, result, decoded, output));
    CHECK(!decode_response(wire, size, result, decoded));
    wire[size] = 0; wire[8] = static_cast<uint8_t>(size + 1 - header_size); wire[9] = static_cast<uint8_t>((size + 1 - header_size) >> 8);
    CHECK(!decode_save_response(wire, size + 1, result, decoded, output));
    for (unsigned which = 0; which < 6; ++which) {
        auto bad = s;
        if (which == 0) bad.mutation_available = 1;
        if (which == 1) bad.fields[SC_SAVE_NATIVE_COMPLETION] = {SC_OBSERVATION_OBSERVED, SC_REASON_NONE, 1, 0, 0};
        if (which == 2) bad.fields[SC_SAVE_PROVIDER].value = SC_SAVE_PROVIDER_FOREIGN + 1;
        if (which == 3) bad.fields[SC_SAVE_JOB_WITNESS].value = 2;
        if (which == 4) bad.fields[SC_SAVE_SELECTED_SLOT].value = 1;
        if (which == 5) bad.profile = SC_PROFILE_NONE;
        size = encode_save_response(wire, WireResult::ok, host, bad); CHECK(!decode_save_response(wire, size, result, decoded, output));
    }
    size = encode_save_response(wire, WireResult::unsupported_operation, host, s);
    CHECK(size == header_size && decode_save_response(wire, size, result, decoded, output));
    uint16_t operation = 0;
    size = encode_request(wire, save_capability, wire_version, save_operation);
    CHECK(size == 24 && decode_request(wire, size, &operation) == WireResult::ok && operation == 11);
    size = encode_request(wire, native_capability, wire_version, save_operation);
    CHECK(decode_request(wire, size, &operation) == WireResult::capability_unavailable);
    // Existing request operation/capability/payload shapes retain their exact bytes.
    constexpr uint64_t capabilities[] = {0, 1, 2, 4, 8, 16, 16, 16, 32, 32, 32};
    sc_diagnostic_request request{}; request.expected.pid = 42; request.expected.process_created = 123;
    request.expected.instance_id[0] = 9; request.expected.lifecycle_generation = 1;
    request.request_id = 7; request.nonce[0] = 2; request.deadline_ms = 100;
    for (uint16_t op = 1; op <= 10; ++op) {
        size = op < native_operation ? encode_request(wire, capabilities[op], wire_version, op) : encode_native_request(wire, op, request, 3);
        CHECK(size == (op < native_operation ? 24u : (op == native_operation ? 32u : 88u)));
        CHECK(wire[0] == 'S' && wire[1] == 'C' && wire[2] == 'I' && wire[3] == 'P' && wire[4] == 1 && wire[5] == 0);
        CHECK(wire[6] == op && wire[7] == 0 && wire[8] == size - 16 && wire[9] == 0 && wire[16] == capabilities[op]);
        for (size_t i = 17; i < 24; ++i) CHECK(wire[i] == 0);
        CHECK(decode_request(wire, size, &operation) == WireResult::ok && operation == op);
    }
    std::puts("PASS production save observation/ownership/races/bounds/unknown/completion refusal/layout/op11/legacy requests; HARNESS ONLY");
}
