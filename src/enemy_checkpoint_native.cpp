#include "enemy_checkpoint_native.h"
#include "native_target.h"
#include "save_collector.h"
#include "commands_native.h"
#include <MinHook.h>
#include <cstring>
#include <cstdio>
#include <atomic>

namespace sentinel::expanded {
namespace {
using SaveCheckpoint = uint64_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using SerializeSpawn = void(*)(uintptr_t, uintptr_t);
using SerializeString = void(*)(uintptr_t, uintptr_t, const char*, save::NativeString*);
using StringLifetime = void(*)(save::NativeString*);
using ReadBlock = bool(*)(uintptr_t, uintptr_t, uintptr_t, uint32_t);
SaveCheckpoint original_save = nullptr;
SerializeSpawn original_spawn = nullptr;
SerializeString serialize_string = nullptr;
StringLifetime construct_string = nullptr, destroy_string = nullptr;
ReadBlock read_block = nullptr;
RecipeCheckpointCalls calls;
std::atomic<bool> checkpoint_enabled{false};
constexpr size_t maximum_bytes = 8 * 1024 * 1024;
constexpr char field[] = "doomEAPEnemyRecipeState";
struct Snapshot { uintptr_t prepared; std::string hex; Snapshot* previous; bool written = false; };
thread_local Snapshot* active = nullptr;

bool encode(std::string_view bytes, std::string& out) {
    if (bytes.size() > maximum_bytes) return false;
    constexpr char digits[] = "0123456789abcdef";
    out.clear(); out.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) { out += digits[byte >> 4]; out += digits[byte & 15]; }
    return true;
}
bool decode(const save::NativeString& text, std::string& out) {
    if (text.length < 0 || static_cast<size_t>(text.length) > maximum_bytes * 2 || text.length % 2 ||
        (text.length && !text.data)) return false;
    out.clear(); out.reserve(text.length / 2);
    auto digit = [](char value) -> int {
        return value >= '0' && value <= '9' ? value - '0' : value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
    };
    for (int32_t i = 0; i < text.length; i += 2) {
        const auto high = digit(text.data[i]), low = digit(text.data[i + 1]);
        if (high < 0 || low < 0) return false;
        out += static_cast<char>((high << 4) | low);
    }
    return true;
}
bool capture(Snapshot& snapshot) {
    try {
        std::string bytes;
        if (!calls.capture(bytes)) {
            commands::diagnostic_trace("AP_CHECKPOINT stage=capture result=failed reason=recipe_capture_refused"); return false;
        }
        const bool encoded = encode(bytes, snapshot.hex);
        commands::diagnostic_trace("AP_CHECKPOINT stage=capture result=%s reason=%s bytes=%zu", encoded ? "ok" : "failed",
            encoded ? "encoded" : "size_limit", bytes.size());
        return encoded;
    } catch (...) { commands::diagnostic_trace("AP_CHECKPOINT stage=capture result=failed reason=exception"); return false; }
}
bool capture_locked(Snapshot& snapshot) {
    calls.lock();
    bool result = false;
    __try { result = capture(snapshot); }
    __finally { calls.unlock(); }
    return result;
}
Snapshot* new_snapshot(uintptr_t world) {
    try { return new Snapshot{world + 8, {}, active}; }
    catch (...) { return nullptr; }
}
void close_snapshot(Snapshot* snapshot) {
    if (snapshot) { active = snapshot->previous; delete snapshot; }
}
void serialize_spawn(uintptr_t prepared, uintptr_t archive) {
    original_spawn(prepared, archive);
    if (!active || active->prepared != prepared || !*reinterpret_cast<const uint8_t*>(archive)) return;
    active->written = false;
    if (!capture_locked(*active)) return;
    save::NativeString value{};
    construct_string(&value);
    const auto table = value.vtable;
    destroy_string(&value);
    value = {table, active->hex.data(), static_cast<int32_t>(active->hex.size()),
             static_cast<uint32_t>(active->hex.size() + 1), {}};
    serialize_string(archive, *reinterpret_cast<const uintptr_t*>(archive + 8), field, &value);
    active->written = true;
    commands::diagnostic_trace("AP_CHECKPOINT stage=enemy_serialization result=written bytes=%zu", active->hex.size());
}
uint64_t save_checkpoint(uintptr_t world, uintptr_t output, uintptr_t request, uintptr_t fourth, uintptr_t fifth) {
    if (!checkpoint_enabled.load(std::memory_order_acquire)) return original_save(world, output, request, fourth, fifth);
    Snapshot* snapshot = nullptr;
    uint64_t result = 0;
    __try {
        snapshot = new_snapshot(world);
        commands::diagnostic_trace("AP_CHECKPOINT stage=native_save_enter world=%llx capture=%s",
            static_cast<unsigned long long>(world), snapshot ? "allocated" : "allocation_failed");
        if (snapshot) {
            active = snapshot;
            result = original_save(world, output, request, fourth, fifth);
            commands::diagnostic_trace("AP_CHECKPOINT stage=native_save_return native_result=%llu enemy_written=%d reason=%s",
                static_cast<unsigned long long>(result), snapshot->written,
                !(result & 0xff) ? "native_refused" : !snapshot->written ? "enemy_serialization_missing_or_failed" : "ok");
            if ((result & 0xff) && !snapshot->written) result &= ~uint64_t{0xff};
        }
    } __finally {
        close_snapshot(snapshot);
    }
    return result;
}
struct ReadFunctor { const uintptr_t* table; std::string* bytes; bool valid; };
bool valid_read(ReadFunctor*) { return true; }
void read_archive(ReadFunctor* self, uintptr_t archive) {
    save::NativeString value{};
    construct_string(&value);
    __try {
        serialize_string(archive, *reinterpret_cast<const uintptr_t*>(archive + 8), field, &value);
        self->valid = decode(value, *self->bytes);
    } __finally { destroy_string(&value); }
}
}
bool recipe_checkpoint_enabled() { return checkpoint_enabled.load(std::memory_order_acquire); }
void stop_recipe_checkpoint() { checkpoint_enabled.store(false, std::memory_order_release); }
bool read_recipe_checkpoint(uintptr_t serializer, std::string_view map, uint32_t native_subtype) {
    if (!recipe_checkpoint_enabled() || !calls.restore) return true;
    if (!serializer || map.empty() || map.size() > 191 || map.find('\0') != map.npos ||
         (native_subtype != 1 && native_subtype != 2)) {
        commands::diagnostic_trace("AP_CHECKPOINT stage=enemy_restore result=failed reason=invalid_context subtype=%u", native_subtype); return false;
    }
    const auto key_text = native_subtype == 1 ? std::string("campaignSpawnInfo") : "missionSelectSpawnInfo_" + std::string(map);
    save::NativeString key{};
    construct_string(&key); const auto table = key.vtable; destroy_string(&key);
    key = {table, const_cast<char*>(key_text.c_str()), static_cast<int32_t>(key_text.size()),
           static_cast<uint32_t>(key_text.size() + 1), {}};
    std::string bytes;
    const uintptr_t callbacks[]{0, reinterpret_cast<uintptr_t>(read_archive), reinterpret_cast<uintptr_t>(valid_read)};
    ReadFunctor functor{callbacks, &bytes, false};
    if (!read_block(serializer, reinterpret_cast<uintptr_t>(&functor), reinterpret_cast<uintptr_t>(&key), 2) ||
        !functor.valid) {
        commands::diagnostic_trace("AP_CHECKPOINT stage=enemy_restore result=failed reason=archive_read_or_decode"); return false;
    }
    try {
        const bool restored = calls.restore(bytes, map);
        commands::diagnostic_trace("AP_CHECKPOINT stage=enemy_restore result=%s reason=%s bytes=%zu map=%.*s", restored ? "ok" : "failed",
            restored ? "accepted" : "recipe_state_refused", bytes.size(), static_cast<int>(map.size()), map.data());
        return restored;
    } catch (...) { commands::diagnostic_trace("AP_CHECKPOINT stage=enemy_restore result=failed reason=exception"); return false; }
}
bool install_recipe_checkpoint(const engine::Binding& binding, HANDLE stop_event, const RecipeCheckpointCalls& callbacks) {
    if (binding.metadata.profile != SC_PROFILE_STEAM_20260818 || !callbacks.lock || !callbacks.unlock ||
        !callbacks.capture || !callbacks.restore) return false;
    constexpr uint32_t rvas[]{0x6a8b80, 0x17df430, 0x684530, 0x3fa8e0, 0x3facb0, 0x55b8d0};
    constexpr const char* prefixes[]{
        "405553574156488dac245897feffb8a8690100e808321c02482be0488b05365e",
        "4055535741564157488dac2450c0ffffb8b0400000e856c90801482be0488b05",
        "48895c24085556574883ec60488b0595a4b2034833c44889442450803900498b",
        "488d0591cb6602c7411414000080488901488d411848894108c7411000000000",
        "40534883ec20488b5108488d05b7c76602488901488bd94885d2742d8b4114c1",
        "40555356574156488d6c24c04881ec40010000488b05ee30c5034833c4488945"};
    native::Target targets[6]{};
    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
    for (size_t index = 0; index < std::size(targets); ++index) {
        targets[index].address = binding.image.base + rvas[index];
        for (size_t byte = 0; byte < targets[index].bytes.size(); ++byte) {
            unsigned value = 0;
            if (sscanf_s(prefixes[index] + byte * 2, "%2x", &value) != 1) return false;
            targets[index].bytes[byte] = static_cast<uint8_t>(value);
        }
        if (index == 3 ? native::validate_leaf_target(memory, binding.image, targets[index], 39, stop_event, deadline) :
                         native::validate_target(memory, binding.image, targets[index], stop_event, deadline)) return false;
    }
    calls = callbacks;
    serialize_string = reinterpret_cast<SerializeString>(targets[2].address);
    construct_string = reinterpret_cast<StringLifetime>(targets[3].address);
    destroy_string = reinterpret_cast<StringLifetime>(targets[4].address);
    read_block = reinterpret_cast<ReadBlock>(targets[5].address);
    void* sites[]{reinterpret_cast<void*>(targets[0].address), reinterpret_cast<void*>(targets[1].address)};
    void* detours[]{reinterpret_cast<void*>(save_checkpoint), reinterpret_cast<void*>(serialize_spawn)};
    void** originals[]{reinterpret_cast<void**>(&original_save), reinterpret_cast<void**>(&original_spawn)};
    for (size_t i = 0; i < std::size(sites); ++i) {
        if (MH_CreateHook(sites[i], detours[i], originals[i]) == MH_OK) continue;
        while (i) MH_RemoveHook(sites[--i]);
        return false;
    }
    for (auto site : sites) {
        if (MH_EnableHook(site) == MH_OK) continue;
        for (auto other : sites) MH_DisableHook(other);
        return false;
    }
    checkpoint_enabled.store(true, std::memory_order_release);
    return true;
}
#ifdef SC_NATIVE_TESTING
bool test_recipe_checkpoint() {
    const auto previous_enabled = checkpoint_enabled.exchange(true);
    const auto previous_calls = calls;
    const auto previous_save = original_save;
    const auto previous_spawn = original_spawn;
    const auto previous_string = serialize_string;
    const auto previous_construct = construct_string, previous_destroy = destroy_string;
    const auto previous_read = read_block;
    static unsigned locks, unlocks, saves, strings, native_serializations, captures;
    static bool capture_ok, emit, save_ok, lock_valid, borrowed;
    static std::string captured, restored, archive_text;
    locks = unlocks = saves = strings = native_serializations = captures = 0;
    capture_ok = emit = save_ok = lock_valid = true;
    borrowed = true;
    captured.assign("checkpoint\0recipe", 17); restored.clear(); archive_text.clear();
    calls = {[] { ++locks; }, [] { ++unlocks; },
        [](std::string& out) { ++captures; lock_valid &= locks == unlocks + 1; out = captured; return capture_ok; },
        [](std::string_view bytes, std::string_view map) {
            if (map != "game/test") return false;
            restored.assign(bytes); return true;
        }};
    construct_string = [](save::NativeString* out) { *out = {}; out->vtable = 77; ++strings; };
    destroy_string = [](save::NativeString*) { --strings; };
    serialize_string = [](uintptr_t archive, uintptr_t root, const char* key, save::NativeString* value) {
        if (std::strcmp(key, field) || root != 91 || value->vtable != 77) return;
        if (*reinterpret_cast<uint8_t*>(archive)) {
            borrowed &= !(value->capacity_flags & 0x40000000U);
            archive_text.assign(value->data, value->length);
        }
        else { value->data = archive_text.data(); value->length = static_cast<int32_t>(archive_text.size()); }
    };
    original_spawn = [](uintptr_t prepared, uintptr_t) {
        ++native_serializations;
        if (prepared == 108) captured += "prepared";
    };
    original_save = [](uintptr_t world, uintptr_t, uintptr_t, uintptr_t, uintptr_t) -> uint64_t {
        ++saves;
        lock_valid &= locks == unlocks;
        uintptr_t archive[]{1, 91, 0, 0};
        if (emit) {
            serialize_spawn(world + 16, reinterpret_cast<uintptr_t>(archive));
            serialize_spawn(world + 8, reinterpret_cast<uintptr_t>(archive));
        }
        return save_ok ? 1 : 0;
    };
    read_block = [](uintptr_t serializer, uintptr_t callback, uintptr_t key, uint32_t kind) {
        if (serializer != 81 || kind != 2) return false;
        const auto name = reinterpret_cast<save::NativeString*>(key)->data;
        borrowed &= !(reinterpret_cast<save::NativeString*>(key)->capacity_flags & 0x40000000U);
        if (std::strcmp(name, "missionSelectSpawnInfo_game/test") && std::strcmp(name, "campaignSpawnInfo")) return false;
        uintptr_t archive[]{0, 91, 0, 0};
        read_archive(reinterpret_cast<ReadFunctor*>(callback), reinterpret_cast<uintptr_t>(archive)); return true;
    };
    const auto before = captured;
    bool ok = save_checkpoint(100, 0, 0, 0, 0) == 1 && lock_valid && locks == unlocks && !active && strings == 0 &&
        native_serializations == 2 && captures == 1 && captured != before &&
        read_recipe_checkpoint(81, "game/test", 2) && restored == captured;
    ok = ok && read_recipe_checkpoint(81, "game/test", 1) && restored == captured &&
        !read_recipe_checkpoint(81, "game/test", 6);
    const auto valid = archive_text;
    archive_text[0] = 'z'; restored = "unchanged";
    ok = ok && !read_recipe_checkpoint(81, "game/test", 2) && restored == "unchanged" && strings == 0;
    archive_text = valid;
    capture_ok = false;
    ok = ok && !save_checkpoint(100, 0, 0, 0, 0) && saves == 2 && locks == unlocks && !active;
    capture_ok = true; emit = false;
    ok = ok && !save_checkpoint(100, 0, 0, 0, 0) && locks == unlocks && !active;
    emit = true; save_ok = false;
    ok = ok && !save_checkpoint(100, 0, 0, 0, 0) && locks == unlocks && !active;
    size_t diagnostic_bytes = 0;
    bool capture_failure_logged = false;
    for (const auto& line : commands::diagnostic_history()) {
        diagnostic_bytes += line.size();
        capture_failure_logged |= line.find("reason=recipe_capture_refused") != std::string::npos;
    }
    ok = ok && capture_failure_logged && diagnostic_bytes <= 32768;
    calls = previous_calls; original_save = previous_save; original_spawn = previous_spawn;
    serialize_string = previous_string; construct_string = previous_construct; destroy_string = previous_destroy;
    read_block = previous_read;
    checkpoint_enabled.store(previous_enabled);
    return ok && borrowed;
}
#endif
}
