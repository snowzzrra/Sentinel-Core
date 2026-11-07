#include "../src/enemy_checkpoint_native.cpp"

namespace sentinel::expanded {
struct RecipeCheckpointChecks {
    inline static unsigned locks, unlocks, saves, strings, native_serializations, captures;
    inline static bool capture_ok, emit, save_ok, lock_valid, borrowed;
    inline static std::string captured, restored, archive_text;
    void install_callbacks() {
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
}
    bool check_round_trip() {
    const auto before = captured;
    bool ok = save_checkpoint(100, 0, 0, 0, 0) == 1 && lock_valid && locks == unlocks && !active && strings == 0 &&
        native_serializations == 2 && captures == 1 && captured != before &&
        read_recipe_checkpoint(81, "game/test", 2) && restored == captured;
    ok = ok && read_recipe_checkpoint(81, "game/test", 1) && restored == captured &&
        !read_recipe_checkpoint(81, "game/test", 6);
    return ok;
}
    void check_capture_failures(bool& ok) {
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
}
    bool run() {
    const auto previous_enabled = checkpoint_enabled.exchange(true);
    const auto previous_calls = calls;
    const auto previous_save = original_save;
    const auto previous_spawn = original_spawn;
    const auto previous_string = serialize_string;
    const auto previous_construct = construct_string, previous_destroy = destroy_string;
    const auto previous_read = read_block;
    locks = unlocks = saves = strings = native_serializations = captures = 0;
    capture_ok = emit = save_ok = lock_valid = true;
    borrowed = true;
    captured.assign("checkpoint\0recipe", 17); restored.clear(); archive_text.clear();
    install_callbacks();
    bool ok=check_round_trip();
    check_capture_failures(ok);
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
};
bool test_recipe_checkpoint() { return RecipeCheckpointChecks{}.run(); }
}
