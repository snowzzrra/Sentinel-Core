#include "engine_observer.h"
#include "protocol.h"
#include "pipe_io.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL engine line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
using namespace sentinel;
using namespace sentinel::engine;
constexpr uintptr_t base = 0x140000000, root = base + 0x45ea6f0, map_address = 0x200000000;
constexpr uintptr_t vtable = base + 0x2a1b000, player = 0x300000000;
constexpr char hash[] = "9809708c823f8db4304201ab0006ff2395d0d484ae9c846d98a56ce8c87c1247";
struct Fake final : Memory {
    std::map<uintptr_t, std::vector<uint8_t>> blocks;
    uintptr_t reject = 0, change = 0;
    uint32_t reject_reason = SC_REASON_READ_FAILED;
    unsigned map_reads = 0, copies = 0;
    template<class T> void put(uintptr_t address, T value) {
        auto& block = blocks[address]; block.resize(sizeof(value)); std::memcpy(block.data(), &value, sizeof(value));
    }
    ReadResult copy(uintptr_t address, void* out, size_t size) override {
        ++copies;
        if (address == reject) return {reject_reason, ERROR_PARTIAL_COPY};
        if (address == root + 0x50 && change && ++map_reads == 2) put(root + 0x50, change);
        auto it = blocks.upper_bound(address);
        if (it == blocks.begin()) return {SC_REASON_READ_FAILED, ERROR_NOACCESS};
        --it;
        if (address - it->first > it->second.size() || size > it->second.size() - (address - it->first))
            return {SC_REASON_PARTIAL_READ, ERROR_PARTIAL_COPY};
        std::memcpy(out, it->second.data() + (address - it->first), size); return {};
    }
};
struct Fixture {
    Fake memory;
    Image image{base, 0x7431000, 0x6a7b9b8c, 0x286caa8, IMAGE_FILE_MACHINE_AMD64,
        {{0x42f300, 0x100, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE},
         {0x2a1b000, 0x100, IMAGE_SCN_MEM_READ},
         {0x388c000, 0x3473834, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE}}};
    Fixture() {
        auto& code = memory.blocks[base + 0x42f300]; code.resize(0x100);
        const uint8_t sig[] = {0x48,0x8d,0x0d,0x79,0xb3,0x1b,0x04,0xe8,0xa4,0x5f,0x23,0,0x84,0xc0,0x48,0x8d,0x0d,3,0xca,0x63,2,0x49,0x8b,0xd4};
        std::memcpy(code.data() + 0x70, sig, sizeof(sig));
        memory.put(root, vtable); memory.put(vtable, base + 0x42f370);
        memory.put(root + 0x50, map_address); memory.put(map_address + 0x1af8, player);
        memory.put(base + 0x5477298, uint8_t(0)); memory.put(base + 0x6b92f18, uint8_t(1));
        memory.put(base + 0x5412948, int32_t(0));
    }
    Binding binding(const char* identity = hash) {
        return bind(memory, image, identity, SC_REASON_NONE, nullptr, GetTickCount64() + 2000);
    }
};
void values_and_failures() {
    Fixture f; auto b = f.binding();
    CHECK(b.metadata.profile == SC_PROFILE_STEAM_20260818 && b.root == root);
    auto s = sample(f.memory, b, 1);
    CHECK(s.sequence == 1 && s.sampled_at_ms && s.sample_reason == SC_REASON_NONE);
    for (auto field : s.fields) CHECK(field.validity == SC_OBSERVATION_OBSERVED);
    CHECK(s.fields[SC_ENGINE_LOADING].value == 0 && s.fields[SC_ENGINE_PLAYER_PRESENT].value == 1);
    CHECK(s.profile_runtime_validated == 0);
    const auto stale = freshness(s, s.sampled_at_ms + 1001);
    CHECK(stale.sample_reason == SC_REASON_STALE && stale.sequence == s.sequence);
    for (auto field : stale.fields) CHECK(field.validity == SC_OBSERVATION_UNKNOWN && field.value == 0);
    CHECK(freshness(s, s.sampled_at_ms + 1000).fields[SC_ENGINE_PLAYER_PRESENT].value == 1);
    f.memory.put(map_address + 0x1af8, uintptr_t(0));
    s = sample(f.memory, b, 2);
    CHECK(s.fields[SC_ENGINE_PLAYER_PRESENT].validity == SC_OBSERVATION_OBSERVED && s.fields[SC_ENGINE_PLAYER_PRESENT].value == 0);
    f.memory.reject = map_address + 0x1af8;
    s = sample(f.memory, b, 3);
    CHECK(s.fields[SC_ENGINE_PLAYER_PRESENT].reason == SC_REASON_READ_FAILED);
    for (auto field : s.fields) CHECK(field.validity == SC_OBSERVATION_UNKNOWN && field.value == 0);
    f.memory.reject_reason = SC_REASON_PARTIAL_READ;
    CHECK(sample(f.memory, b, 4).fields[SC_ENGINE_PLAYER_PRESENT].reason == SC_REASON_PARTIAL_READ);
    f.memory.reject = root + 0x50;
    CHECK(sample(f.memory, b, 5).fields[SC_ENGINE_MAP_PRESENT].reason == SC_REASON_PARTIAL_READ);
    f.memory.reject = 0; f.memory.put(root + 0x50, uintptr_t(0));
    s = sample(f.memory, b, 6);
    CHECK(s.fields[SC_ENGINE_MAP_PRESENT].validity == SC_OBSERVATION_OBSERVED && s.fields[SC_ENGINE_MAP_PRESENT].value == 0);
    CHECK(s.fields[SC_ENGINE_PLAYER_PRESENT].reason == SC_REASON_PARENT_NULL);
    f.memory.put(root + 0x50, map_address); f.memory.change = map_address + 0x10000;
    f.memory.put(f.memory.change + 0x1af8, player); f.memory.map_reads = 0;
    s = sample(f.memory, b, 7);
    CHECK(s.sample_reason == SC_REASON_TRANSITION);
    for (auto field : s.fields) CHECK(field.validity == SC_OBSERVATION_UNKNOWN);
    f.memory.change = 0; f.memory.put(base + 0x5477298, uint8_t(2));
    CHECK(sample(f.memory, b, 8).fields[SC_ENGINE_LOADING].reason == SC_REASON_INVALID_VALUE);
    f.memory.put(base + 0x5477298, uint8_t(0)); f.memory.put(root, uintptr_t(0));
    s = sample(f.memory, b, 9);
    CHECK(s.fields[SC_ENGINE_ROOT].validity == SC_OBSERVATION_OBSERVED && s.fields[SC_ENGINE_ROOT].value == 0);
    CHECK(s.fields[SC_ENGINE_MAP_PRESENT].reason == SC_REASON_PARENT_NULL);
    f.memory.put(root, vtable);
    CHECK(sample(f.memory, b, 10).fields[SC_ENGINE_PLAYER_PRESENT].value == 1);
}
void locators_and_identity() {
    Fixture f; auto unknown = f.binding("0000000000000000000000000000000000000000000000000000000000000000");
    CHECK(unknown.root == root && unknown.metadata.profile == SC_PROFILE_NONE);
    const auto before = f.memory.copies;
    const auto s = sample(f.memory, unknown, 1);
    CHECK(f.memory.copies - before == 4); // Root/vtable twice, NO guessed child/global reads.
    CHECK(s.fields[SC_ENGINE_ROOT].validity == SC_OBSERVATION_PROVISIONAL);
    CHECK(s.fields[SC_ENGINE_MAP_PRESENT].reason == SC_REASON_PROFILE_UNRECOGNIZED);
    CHECK(s.fields[SC_ENGINE_LOADING].reason == SC_REASON_PROFILE_UNRECOGNIZED);
    f.image.timestamp++;
    CHECK(f.binding().metadata.profile == SC_PROFILE_NONE); f.image.timestamp--;
    f.image.sections[2].flags = IMAGE_SCN_MEM_READ;
    auto rejected = f.binding();
    CHECK(rejected.metadata.root_locator_reason == SC_REASON_OUT_OF_RANGE);
    CHECK(rejected.global_reasons[0] == SC_REASON_OUT_OF_RANGE);
    f.image.sections[2].flags |= IMAGE_SCN_MEM_WRITE;
    auto& code = f.memory.blocks[base + 0x42f300];
    std::memcpy(code.data(), code.data() + 0x70, 24);
    CHECK(f.binding().metadata.root_locator_reason == SC_REASON_SIGNATURE_AMBIGUOUS);
    code.assign(0x100, 0);
    CHECK(f.binding().metadata.root_locator_reason == SC_REASON_SIGNATURE_MISSING);
    f.memory.reject = base + 0x42f300;
    CHECK(f.binding().metadata.root_locator_reason == SC_REASON_READ_FAILED);
    Handle stop(CreateEventW(nullptr, TRUE, TRUE, nullptr));
    CHECK(bind(f.memory, f.image, hash, SC_REASON_NONE, stop.value, GetTickCount64() + 2000).metadata.root_locator_reason == SC_REASON_CANCELLED);
    CHECK(bind(f.memory, f.image, hash, SC_REASON_NONE, nullptr, GetTickCount64()).metadata.root_locator_reason == SC_REASON_BUDGET);
}
void checked_adapter_and_pe() {
    uintptr_t result = 0;
    CHECK(!add(UINTPTR_MAX - 1, 8, 8, result)); CHECK(!add(0x10000, SIZE_MAX, 8, result));
    LocalMemory memory; uint64_t out = 0;
    CHECK(memory.copy(1, &out, sizeof(out)).reason == SC_REASON_OUT_OF_RANGE);
    auto pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)); CHECK(pages);
    DWORD old = 0; CHECK(VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &old));
    CHECK(memory.copy(reinterpret_cast<uintptr_t>(pages + 4092), &out, 8).reason == SC_REASON_READ_FAILED);
    CHECK(VirtualProtect(pages + 4096, 4096, PAGE_READWRITE | PAGE_GUARD, &old));
    CHECK(memory.copy(reinterpret_cast<uintptr_t>(pages + 4096), &out, 8).reason == SC_REASON_READ_FAILED);
    CHECK(VirtualFree(pages, 0, MEM_RELEASE));
    Image image;
    CHECK(read_image(memory, reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), image).reason == SC_REASON_NONE);
    CHECK(image.machine == IMAGE_FILE_MACHINE_AMD64 && image.contains(image.entry, 1, IMAGE_SCN_MEM_EXECUTE, 0));
    Fake malformed; IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x100;
    malformed.put(base, dos);
    CHECK(read_image(malformed, base, image).reason == SC_REASON_PARTIAL_READ);
    dos.e_lfanew = 0x7fffffff; malformed.put(base, dos);
    CHECK(read_image(malformed, base, image).reason == SC_REASON_INVALID_PE);
}
void protocol() {
    Fixture f; auto e = sample(f.memory, f.binding(), 3);
    Snapshot s{}; s.core.abi_version = SC_ABI_VERSION; strcpy_s(s.core.version, "fixture"); strcpy_s(s.core.build_id, hash);
    s.pid = 123; s.process_created = 456; s.instance[0] = 1;
    Message data{}; auto size = encode_engine_response(data, WireResult::ok, s, e);
    CHECK(size <= max_message);
    Snapshot decoded{}; sc_engine_snapshot engine{}; WireResult result{};
    CHECK(decode_engine_response(data, size, result, decoded, engine));
    CHECK(decoded.pid == 123 && engine.sequence == 3 && engine.fields[SC_ENGINE_PLAYER_PRESENT].value == 1);
    CHECK(!decode_response(data, size, result, decoded));
    CHECK(!decode_engine_response(data, size - 1, result, decoded, engine));
    data[size] = 0; CHECK(!decode_engine_response(data, size + 1, result, decoded, engine));
    e.fields[0] = {SC_OBSERVATION_UNKNOWN, SC_REASON_READ_FAILED, 1, 0};
    size = encode_engine_response(data, WireResult::ok, s, e);
    CHECK(!decode_engine_response(data, size, result, decoded, engine));
    e = unavailable(SC_REASON_STOPPED); size = encode_engine_response(data, WireResult::ok, s, e);
    CHECK(decode_engine_response(data, size, result, decoded, engine));
    CHECK(engine.fields[0].reason == SC_REASON_STOPPED && engine.fields[0].value == 0);
    uint16_t op = 0;
    size = encode_request(data, engine_capability, wire_version, engine_operation);
    CHECK(decode_request(data, size, &op) == WireResult::ok && op == engine_operation);
    size = encode_request(data, 4, wire_version, engine_operation);
    CHECK(decode_request(data, size) == WireResult::capability_unavailable);
    size = encode_request(data, engine_capability, 99, engine_operation);
    CHECK(decode_request(data, size) == WireResult::incompatible_protocol);
}
int main() {
    values_and_failures(); locators_and_identity(); checked_adapter_and_pe(); protocol();
    std::puts("PASS bounded reader, profile/locator gates, null/read failures, transition/no stale, wire contract; SYNTHETIC ONLY");
}
