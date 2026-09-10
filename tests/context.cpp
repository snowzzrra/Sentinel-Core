#include "context_observer.h"
#include "protocol.h"
#include <map>
#include <vector>
#include <functional>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL context line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
using namespace sentinel;
struct Fake final : engine::Memory {
    std::map<uintptr_t, std::vector<unsigned char>> blocks;
    std::vector<uintptr_t> reads;
    std::function<void(uintptr_t)> before;
    uintptr_t reject = 0;
    uint32_t failure = SC_REASON_READ_FAILED;
    template<class T> void put(uintptr_t address, const T& value) {
        auto& bytes = blocks[address]; bytes.resize(sizeof(T)); std::memcpy(bytes.data(), &value, sizeof(T));
    }
    void bytes(uintptr_t address, const std::string& value) {
        auto& bytes = blocks[address]; bytes.assign(value.begin(), value.end()); bytes.push_back(0);
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
struct NativeString { uintptr_t vtable, data; int32_t length; uint32_t allocation; };
struct Fixture {
    Fake memory;
    engine::Binding binding;
    uintptr_t root = 0x1445ea6f0ULL, map = 0x200000000ULL, data = 0x300000000ULL;
    Fixture() {
        binding.metadata = engine::unavailable(SC_REASON_NOT_SAMPLED);
        binding.metadata.pe_reason = binding.metadata.disk_hash_reason = binding.metadata.root_locator_reason = SC_REASON_NONE;
        binding.metadata.profile = SC_PROFILE_STEAM_20260818; binding.metadata.locator_revision = 1;
        strcpy_s(binding.metadata.disk_sha256, "9809708c823f8db4304201ab0006ff2395d0d484ae9c846d98a56ce8c87c1247");
        binding.image.base = 0x140000000ULL; binding.root = root;
        memory.put(root, uintptr_t{0x142aaa730ULL}); memory.put(root + 0x50, map);
        state(SC_GAME_IN_GAME, 12); name("game/sp/e3m2_hell_b/e3m2_hell_b");
    }
    void state(uint32_t value, uint32_t changed) { const uint32_t pair[]{value, changed}; memory.put(root + 0x44, pair); }
    void name(const std::string& value) {
        memory.put(map, uintptr_t{0x142ab30c8ULL}); memory.bytes(data, value);
        memory.put(map + 0x9a060, NativeString{0x142a67478ULL, data, static_cast<int32_t>(value.size()), 0xc0001000});
    }
    sc_context_snapshot sample(uint64_t sequence = 1, HANDLE stop = nullptr) { return context::sample(memory, binding, sequence, stop); }
};
void unknown_map(const sc_context_snapshot& s, uint32_t reason) {
    CHECK(s.current_map.validity == SC_OBSERVATION_UNKNOWN && s.current_map.reason == reason && s.current_map.length == 0);
    for (char c : s.current_map.bytes) CHECK(c == 0);
}
int main() {
    Fixture f; auto s = f.sample();
    CHECK(s.current_map.validity == SC_OBSERVATION_OBSERVED);
    CHECK(std::strcmp(s.current_map.bytes, "game/sp/e3m2_hell_b/e3m2_hell_b") == 0);
    CHECK(s.fields[SC_CONTEXT_GAME_STATE].value == SC_GAME_IN_GAME && s.layout_revision == 1);
    for (size_t i = SC_CONTEXT_LOAD_SERIAL; i < SC_CONTEXT_FIELD_COUNT; ++i)
        CHECK(s.fields[i].reason == SC_CONTEXT_UNSUPPORTED && s.fields[i].validity == SC_OBSERVATION_UNKNOWN);
    f.name("game/sp/e3m2_hell/e3m2_hell"); CHECK(std::strcmp(f.sample().current_map.bytes, s.current_map.bytes) != 0);
    f.state(SC_GAME_MAIN_MENU, 22); unknown_map(f.sample(), SC_CONTEXT_MENU_WORLD);
    f.state(SC_GAME_LOADING, 23); unknown_map(f.sample(), SC_REASON_TRANSITION);
    f.state(SC_GAME_IN_GAME, UINT32_MAX); CHECK(f.sample().fields[SC_CONTEXT_STATE_CHANGED_MS].value == UINT32_MAX);
    f.state(SC_GAME_IN_GAME, 0); CHECK(f.sample(2).fields[SC_CONTEXT_LOAD_SERIAL].reason == SC_CONTEXT_UNSUPPORTED);
    { Fixture t; t.memory.put(t.root + 0x50, uintptr_t{0}); unknown_map(t.sample(), SC_REASON_PARENT_NULL); }
    { Fixture t; t.binding.metadata.profile = SC_PROFILE_NONE; unknown_map(t.sample(), SC_REASON_PROFILE_UNRECOGNIZED); CHECK(t.memory.reads.empty()); }
    { Fixture t; t.memory.reject = t.data; t.memory.failure = SC_REASON_PARTIAL_READ; auto v = t.sample();
      unknown_map(v, SC_REASON_PARTIAL_READ); CHECK(v.current_map.win32_error == ERROR_PARTIAL_COPY);
      CHECK(v.fields[SC_CONTEXT_GAME_STATE].validity == SC_OBSERVATION_OBSERVED); }
    { Fixture t; t.memory.reject = 0x145412948ULL; CHECK(t.sample().current_map.validity == SC_OBSERVATION_OBSERVED);
      for (auto address : t.memory.reads) CHECK(address != t.memory.reject && address != t.map + 0x1af8); }
    { Fixture t; t.memory.put(t.map, uintptr_t{0x142aaa730ULL}); unknown_map(t.sample(), SC_REASON_INVALID_VALUE); }
    { Fixture t; t.name(""); unknown_map(t.sample(), SC_CONTEXT_EMPTY_NAME); }
    { Fixture t; t.data = t.map + 0x9a078; t.name("game/hub/hub");
      t.memory.put(t.map + 0x9a060, NativeString{0x142a67478ULL, t.data, 12, 0x80000014});
      CHECK(std::strcmp(t.sample().current_map.bytes, "game/hub/hub") == 0); }
    { Fixture t; t.name(std::string(255, 'x')); CHECK(t.sample().current_map.length == 255);
      t.name(std::string(256, 'x')); unknown_map(t.sample(), SC_CONTEXT_NAME_TOO_LONG); }
    for (const auto length : {-1, 4096}) { Fixture t;
        t.memory.put(t.map + 0x9a060, NativeString{0x142a67478ULL, t.data, length, 4096}); unknown_map(t.sample(), SC_REASON_INVALID_VALUE); }
    { Fixture t; t.memory.blocks[t.data].back() = 'x'; unknown_map(t.sample(), SC_REASON_INVALID_VALUE); }
    { Fixture t; t.memory.blocks[t.data][4] = 0; unknown_map(t.sample(), SC_REASON_INVALID_VALUE); }
    { Fixture t; t.memory.blocks[t.data][4] = 0xff; unknown_map(t.sample(), SC_REASON_INVALID_VALUE); }
    { Fixture t; t.memory.put(t.map + 0x9a060, NativeString{0x142a67478ULL, 0x7ffffffffffeULL, 8, 20}); unknown_map(t.sample(), SC_REASON_OUT_OF_RANGE); }
    { Fixture t; unsigned visits = 0;
      t.memory.before = [&](uintptr_t address) { if (address == t.root + 0x44 && ++visits == 2) t.state(SC_GAME_LOADING, 13); };
      unknown_map(t.sample(), SC_REASON_TRANSITION); }
    { Fixture t; unsigned visits = 0;
      t.memory.before = [&](uintptr_t address) { if (address == t.data && ++visits == 2) t.memory.blocks[t.data][12] ^= 1; };
      unknown_map(t.sample(), SC_REASON_TRANSITION); }
    { Fixture t; const auto old_map = t.map; t.map += 0x200000; t.data += 0x200000; t.name("game/sp/other/other"); unsigned visits = 0;
      t.memory.before = [&](uintptr_t address) { if (address == t.root + 0x50 && ++visits == 2) t.memory.put(t.root + 0x50, t.map); };
      CHECK(old_map != t.map); unknown_map(t.sample(), SC_REASON_TRANSITION); }
    { Fixture t; HANDLE stop = CreateEventW(nullptr, TRUE, TRUE, nullptr); CHECK(stop);
      unknown_map(t.sample(1, stop), SC_REASON_CANCELLED); CHECK(t.memory.reads.empty()); CloseHandle(stop); }
    s.sampled_at_ms = 100;
    unknown_map(context::freshness(s, 1101), SC_REASON_STALE);
    unknown_map(context::freshness(s, 99), SC_REASON_STALE);
    CHECK(context::freshness(s, 1100).current_map.validity == SC_OBSERVATION_OBSERVED);
    // Each field is encoded explicitly; no C padding or live addresses cross IPC.
    Snapshot host{}; host.pid = 42; host.process_created = 123; host.instance[0] = 9;
    host.core.abi_version = SC_ABI_VERSION; strcpy_s(host.core.version, "0.4.0"); strcpy_s(host.core.build_id, "fixture");
    Message wire{}; Snapshot decoded{}; sc_context_snapshot output{}; WireResult result{};
    auto size = encode_context_response(wire, WireResult::ok, host, s);
    CHECK(decode_context_response(wire, size, result, decoded, output));
    CHECK(output.pid == 42 && output.process_created == 123 && output.instance[0] == 9);
    CHECK(std::strcmp(output.current_map.bytes, s.current_map.bytes) == 0);
    for (size_t n = 0; n < size; ++n) CHECK(!decode_context_response(wire, n, result, decoded, output));
    CHECK(!decode_response(wire, size, result, decoded));
    sc_engine_snapshot legacy{}; CHECK(!decode_engine_response(wire, size, result, decoded, legacy));
    wire[size] = 1; wire[8] = static_cast<uint8_t>(size + 1 - header_size); wire[9] = static_cast<uint8_t>((size + 1 - header_size) >> 8);
    CHECK(!decode_context_response(wire, size + 1, result, decoded, output));
    auto bad = s; bad.current_map.length = SC_CONTEXT_MAP_CAPACITY - 1; // Bounded long names also fit the wire.
    std::memset(bad.current_map.bytes, 'q', SC_CONTEXT_MAP_CAPACITY - 1); bad.current_map.bytes[SC_CONTEXT_MAP_CAPACITY - 1] = 0;
    size = encode_context_response(wire, WireResult::ok, host, bad); CHECK(size < max_message);
    CHECK(decode_context_response(wire, size, result, decoded, output) && output.current_map.length == 255);
    bad = s; bad.fields[SC_CONTEXT_LOAD_SERIAL] = {SC_OBSERVATION_OBSERVED, SC_REASON_NONE, 1, 0};
    size = encode_context_response(wire, WireResult::ok, host, bad); CHECK(!decode_context_response(wire, size, result, decoded, output));
    bad = s; bad.fields[SC_CONTEXT_GAME_STATE].value = SC_GAME_MAIN_MENU;
    size = encode_context_response(wire, WireResult::ok, host, bad); CHECK(!decode_context_response(wire, size, result, decoded, output));
    bad = s; bad.profile = SC_PROFILE_NONE;
    size = encode_context_response(wire, WireResult::ok, host, bad); CHECK(!decode_context_response(wire, size, result, decoded, output));
    bad = s; bad.current_map.validity = SC_OBSERVATION_UNKNOWN; bad.current_map.reason = SC_REASON_STALE;
    size = encode_context_response(wire, WireResult::ok, host, bad); CHECK(!decode_context_response(wire, size, result, decoded, output));
    size = encode_context_response(wire, WireResult::unsupported_operation, host, s);
    CHECK(size == header_size && decode_context_response(wire, size, result, decoded, output));
    uint16_t operation = 0;
    size = encode_request(wire, context_capability, wire_version, context_operation);
    CHECK(decode_request(wire, size, &operation) == WireResult::ok && operation == context_operation);
    size = encode_request(wire, engine_capability, wire_version, context_operation);
    CHECK(decode_request(wire, size, &operation) == WireResult::capability_unavailable);
    std::puts("PASS context ownership/state/races/bounds/errors/cancellation/freshness/scopes/encoding; HARNESS ONLY");
}
