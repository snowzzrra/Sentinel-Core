#pragma once

struct DarklordSlot {
    uintptr_t definition = 0;
    int32_t index = -1;
};
struct DarklordState {
    uintptr_t actor = 0, vtable = 0, animator = 0, animweb = 0;
    int32_t entity = -1, ai = 0, context = 0, anim_enabled = -1, anim_node = -1, anim_depth = 0;
    uint32_t flags = 0, anim_path_seq = 0, anim_state = 0;
    uint64_t anim_path_hash = 0;
    uint8_t control = 0;
    float health = 0, armor = 0;
    std::array<float, 3> position{};
    bool removed = false, fault = false, flyer = false;
    const char* anim_read = "absent";
};
struct DarklordActor {
    uint32_t handle = 0;
    int32_t slot = -1;
    DarklordState initial{}, last{};
    uint64_t created = 0, sampled = 0, frozen = 0, recovered = 0, state_unknown = 0;
    unsigned next = 0, samples = 0, skipped = 0;
    bool finished = false;
};
struct DarklordCall {
    uint64_t seq = 0, at = 0, game_tick = 0, call_at = 0;
    CostClock::time_point call_start{};
    uintptr_t component = 0, world = 0, boss = 0;
    uint32_t rng = 0, rng_call = 0;
    int32_t before_count = -1;
    std::array<uint32_t, 64> before{};
    std::vector<DarklordSlot> slots;
    std::vector<DarklordActor> actors;
    unsigned lines = 0;
    DWORD thread = GetCurrentThreadId();
    bool before_ok = false;
};
uintptr_t darklord_image = 0;
uint64_t darklord_sequence = 0, darklord_frame = 0, darklord_epoch = 0;
std::map<uint64_t, std::unique_ptr<DarklordCall>> darklord_calls;
constexpr uint64_t darklord_sample_ms[]{250, 500, 1000, 2000, 3000, 5000, 8000, 12000};
using DarklordSink = void(*)(const char*, bool);
DarklordSink darklord_sink = commands::darklord_trace;

template<class T> bool darklord_read(uintptr_t owner, size_t offset, T& value) noexcept {
    engine::LocalMemory memory;
    uintptr_t address = 0;
    value = {};
    return engine::add(owner, offset, sizeof(value), address) &&
        !memory.copy(address, &value, sizeof(value)).reason;
}
bool darklord_text(uintptr_t address, char (&out)[512]) noexcept {
    __try {
        if (!address) { strcpy_s(out, "null"); return true; }
        for (size_t i = 0; i < sizeof(out) - 1; ++i) {
            const auto c = *reinterpret_cast<const unsigned char*>(address + i);
            if (!c) { out[i] = 0; return true; }
            out[i] = c <= 32 || c >= 127 || c == '=' ? '_' : static_cast<char>(c);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    strcpy_s(out, "read_fault"); return false;
}
bool darklord_decl(uintptr_t definition, char (&out)[512]) noexcept {
    uintptr_t name = 0;
    if (!definition) { strcpy_s(out, "null"); return true; }
    if (definition && darklord_read(definition, 8, name)) return darklord_text(name, out);
    strcpy_s(out, "read_fault"); return false;
}
bool darklord_map(uintptr_t world) noexcept {
    uintptr_t name = 0;
    char text[512];
    return world && darklord_read(world, 0x9a068, name) && darklord_text(name, text) &&
        (std::string_view(text) == "e5m4_boss" || std::string_view(text) == "game/dlc2/e5m4_boss/e5m4_boss");
}
uint64_t darklord_game_tick(uintptr_t world) noexcept {
    uint64_t value = 0;
    __try { if (native_clock) native_clock(world + 0x125a60, &value, 1); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return value;
}
void darklord_emit(DarklordCall& call, const char* event, const char* format, ...) noexcept {
    if (call.lines >= 1024) return;
    char detail[3072], line[4096];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf_s(detail, sizeof(detail), _TRUNCATE, format, arguments);
    va_end(arguments);
    if (++call.lines == 1024) { event = "limit"; strcpy_s(detail, "reason=invocation_line_cap"); }
    sprintf_s(line, "AP_DARKLORD event=%s pid=%u seq=%llu at_ms=%llu game_tick=0x%llx frame=%llu frame_kind=expanded_observe epoch=%llu map=e5m4_boss component=0x%llx %s",
        event, GetCurrentProcessId(), call.seq, GetTickCount64(), call.game_tick, darklord_frame,
        darklord_epoch, static_cast<unsigned long long>(call.component), detail);
    darklord_sink(line, GetCurrentThreadId() == call.thread);
}
bool darklord_handles(uintptr_t component, std::array<uint32_t, 64>& handles, int32_t& count) noexcept {
    uintptr_t data = 0;
    count = -1;
    if (!darklord_read(component, 0x3e0, count)) { count = -1; return false; }
    if (count < 0 || count > static_cast<int32_t>(handles.size()) ||
        !darklord_read(component, 0x3d8, data)) return false;
    engine::LocalMemory memory;
    return !count || (data && !memory.copy(data, handles.data(), static_cast<size_t>(count) * 4).reason);
}
uintptr_t darklord_resolve(uintptr_t world, uint32_t handle, bool& fault) noexcept {
    uintptr_t table = 0, function = 0;
    if (!darklord_read(world, 0, table) || !darklord_read(table, 0x88, function) ||
        function != darklord_image + 0x6998e0) { fault = true; return 0; }
    __try { return reinterpret_cast<uintptr_t(*)(uintptr_t, uint32_t*)>(function)(world, &handle); }
    __except(EXCEPTION_EXECUTE_HANDLER) { fault = true; return 0; }
}
DarklordState darklord_state(uintptr_t world, uint32_t handle, uintptr_t actor) noexcept {
    DarklordState out; out.actor = actor;
    uintptr_t registered = 0, flyer = 0, data = 0, anim_table = 0, enable = 0;
    uint32_t identity = 0, alternate = 0;
    if (!actor) { out.removed = true; return out; }
    if (!darklord_read(actor, 0x10, out.entity)) { out.fault = true; return out; }
    if (out.entity < 0 || out.entity >= 0x3ffe) { out.removed = true; return out; }
    if (!darklord_read(world, 0x1af8 + static_cast<size_t>(out.entity) * 8, registered) ||
        !darklord_read(world, 0x21af8 + static_cast<size_t>(out.entity) * 4, identity) ||
        !darklord_read(world, 0x31af8 + static_cast<size_t>(out.entity) * 4, alternate)) {
        out.fault = true; return out;
    }
    if (registered != actor || (identity != handle && alternate != handle)) { out.removed = true; return out; }
    out.fault = !darklord_read(actor, 0, out.vtable);
    out.fault |= !darklord_read(actor, 0x74, out.position);
    out.fault |= !darklord_read(actor, 0x5634, out.ai);
    out.fault |= !darklord_read(actor, 0xa18, out.context);
    out.fault |= !darklord_read(actor, 0x2a0, out.flags);
    out.fault |= !darklord_read(actor, 0x16820, out.control);
    out.fault |= !darklord_read(actor, 0x16670, flyer);
    out.flyer = flyer != 0;
    out.fault |= !darklord_read(actor, 0x65a4, out.health);
    out.fault |= !darklord_read(actor, 0x6654, out.armor);
    out.fault |= !std::isfinite(out.health) || !std::isfinite(out.armor);
    for (auto value : out.position) out.fault |= !std::isfinite(value);
    if (!darklord_read(actor, 0x5960, data)) out.anim_read = "read_fault";
    else if (data) {
        if (!engine::add(data, 0x9890, sizeof(uintptr_t), out.animator)) { out.anim_read = "read_fault"; return out; }
        if (!darklord_read(out.animator, 0, anim_table) || !darklord_read(anim_table, 0x98, enable))
            out.anim_read = "read_fault";
        else if (enable != darklord_image + 0x4947b0) out.anim_read = "unsupported_vtable";
        else {
            uint8_t enabled = 0;
            std::array<uint16_t, 32> path{};
            if (!darklord_read(out.animator, 0x370, enabled) || !darklord_read(out.animator, 0x358, out.animweb) ||
                !darklord_read(out.animator, 0x5ac, path) || !darklord_read(out.animator, 0x4bc, out.anim_path_seq) ||
                !darklord_read(out.animator, 0x500, out.anim_state) || !darklord_read(out.animator, 0x5ec, out.anim_depth))
                out.anim_read = "read_fault";
            else if (enabled > 1 || out.anim_depth < 0 || out.anim_depth > static_cast<int32_t>(path.size()))
                out.anim_read = "invalid_value";
            else {
                out.anim_read = "ok"; out.anim_enabled = enabled;
                out.anim_node = out.anim_depth ? static_cast<int16_t>(path[out.anim_depth - 1]) : -1;
                out.anim_path_hash = 14695981039346656037ull;
                for (int32_t i = 0; i < out.anim_depth; ++i) { out.anim_path_hash ^= path[i]; out.anim_path_hash *= 1099511628211ull; }
            }
        }
    }
    return out;
}
float darklord_distance(const DarklordState& a, const DarklordState& b) {
    float squared = 0;
    for (size_t i = 0; i < 3; ++i) { const auto delta = a.position[i] - b.position[i]; squared += delta * delta; }
    return std::sqrt(squared);
}
bool darklord_same_state(const DarklordState& a, const DarklordState& b) {
    return a.ai == b.ai && a.context == b.context && a.flags == b.flags && a.control == b.control &&
        a.anim_enabled == b.anim_enabled && a.anim_path_hash == b.anim_path_hash && a.animator == b.animator &&
        a.animweb == b.animweb && a.anim_state == b.anim_state && a.fault == b.fault && a.removed == b.removed &&
        !std::strcmp(a.anim_read, b.anim_read);
}
void darklord_actor_line(DarklordCall& call, DarklordActor& actor, const char* event, const char* reason,
                        uint64_t now, uint64_t due = 0) noexcept {
    const auto& s = actor.last;
    darklord_emit(call, event, "path=managed_boss_handle_diff handle=0x%x slot=%d actor=0x%llx entity=%d vtable=0x%llx vtable_rva=0x%llx age_ms=%llu due_ms=%llu x=%g y=%g z=%g delta=%g ai_override=%d context_a18=0x%x flags_2a0=0x%x control_16820=0x%x flyer=%u hidden=unknown dormant=unknown health=%g armor=%g alive=%s removed=%u read_fault=%u animator=0x%llx anim_read=%s anim_enabled=%d animweb=0x%llx anim_node=%d anim_path_hash=0x%llx anim_path_seq=%u anim_depth=%d anim_state=0x%x frozen_ms=%llu recovered_at_ms=%lld samples=%u skipped_samples=%u state_unknown_ms=%llu reason=%s",
        actor.handle, actor.slot, static_cast<unsigned long long>(s.actor), s.entity,
        static_cast<unsigned long long>(s.vtable), static_cast<unsigned long long>(s.vtable ? s.vtable - darklord_image : 0),
        now - actor.created, due, s.position[0], s.position[1], s.position[2], darklord_distance(s, actor.initial),
        s.ai, static_cast<uint32_t>(s.context), s.flags, s.control, s.flyer ? 1u : 0u, s.health, s.armor,
        s.fault ? "unknown" : s.removed || (s.flags & 0x10000) ? "0" : "1", s.removed ? 1u : 0u, s.fault ? 1u : 0u,
        static_cast<unsigned long long>(s.animator), s.anim_read, s.anim_enabled, static_cast<unsigned long long>(s.animweb),
        s.anim_node, s.anim_path_hash, s.anim_path_seq, s.anim_depth, s.anim_state, actor.frozen,
        actor.recovered ? static_cast<long long>(actor.recovered) : -1ll, actor.samples, actor.skipped, actor.state_unknown, reason);
}
void darklord_track(DarklordCall& call, uint32_t handle) {
    bool fault = false;
    const auto actor = darklord_resolve(call.world, handle, fault);
    DarklordActor tracked; tracked.handle = handle; tracked.created = tracked.sampled = GetTickCount64();
    tracked.initial = tracked.last = darklord_state(call.world, handle, actor);
    tracked.last.fault |= fault;
    uintptr_t definition = 0, inherit = 0;
    char name[512] = "read_fault", decl[512] = "read_fault", species[512] = "read_fault";
    if (actor && !tracked.last.removed && !tracked.last.fault) {
        if (!read_instance_name(actor, name)) strcpy_s(name, "read_fault");
        else darklord_text(reinterpret_cast<uintptr_t>(name), name);
        darklord_read(actor, 0xa8, definition); darklord_decl(definition, decl);
        darklord_read(definition, 0x158, inherit); darklord_decl(inherit, species);
        for (const auto& slot : call.slots)
            if (definition == slot.definition) { tracked.slot = slot.index; break; }
    }
    darklord_emit(call, "identity", "path=managed_boss_handle_diff handle=0x%x actor=0x%llx slot=%d entity_name=%s decl=%s inherit=%s read_fault=%u",
        handle, static_cast<unsigned long long>(actor), tracked.slot, name, decl, species, tracked.last.fault ? 1u : 0u);
    darklord_actor_line(call, tracked, "actor", "created", tracked.created);
    tracked.finished = !tracked.last.fault && tracked.last.removed;
    if (tracked.finished) darklord_actor_line(call, tracked, "summary", fault ? "resolve_read_fault" : "read_fault_or_removed", tracked.created);
    call.actors.push_back(tracked);
}
std::unique_ptr<DarklordCall> darklord_begin(uintptr_t component) noexcept {
    uintptr_t world = 0, boss = 0;
    if (!darklord_read(world_global, 0, world) || !darklord_map(world)) return nullptr;
    try {
        if (darklord_calls.size() >= 16) {
            DarklordCall limit; limit.seq = ++darklord_sequence; limit.component = component;
            darklord_emit(limit, "limit", "reason=active_invocation_cap"); return nullptr;
        }
        auto result = std::make_unique<DarklordCall>();
        auto& call = *result;
        call.seq = ++darklord_sequence; call.at = GetTickCount64(); call.world = world; call.component = component;
        call.game_tick = darklord_game_tick(world);
        const bool boss_ok = darklord_read(component, 8, boss); call.boss = boss;
        const bool rng_ok = darklord_read(world, 0x9a038, call.rng);
        call.before_ok = darklord_handles(component, call.before, call.before_count);
        std::array<uint32_t, 6> phase{};
        const bool phase_ok = darklord_read(component, 0x3c0, phase);
        darklord_emit(call, "begin", "path=managed_boss boss=0x%llx rng_before=0x%x count_before=%d component_3c0=0x%x component_3c4=0x%x component_3c8=0x%x component_3cc=0x%x component_3d0=0x%x component_3d4=0x%x read_fault=%u",
            static_cast<unsigned long long>(boss), call.rng, call.before_count, phase[0], phase[1], phase[2], phase[3], phase[4], phase[5],
            boss_ok && rng_ok && call.before_ok && phase_ok ? 0u : 1u);
        return result;
    } catch (...) { return nullptr; }
}
void darklord_slots(DarklordCall* call, uintptr_t component) noexcept {
    if (!call) return;
    try {
        uintptr_t declaration = 0, entries = 0;
        int32_t count = -1;
        if (!darklord_read(component, 0x10, declaration) || !darklord_read(declaration, 0x1e0, entries) ||
            !darklord_read(declaration, 0x1e8, count) || count < 0 || count > 64) {
            darklord_emit(*call, "read_fault", "field=declaration_slots"); return;
        }
        for (int32_t i = 0; i < count; ++i) {
            uintptr_t definition = 0, parent = 0;
            const auto slot = entries + static_cast<size_t>(i) * 16 + 8;
            char decl[512] = "read_fault", inherit[512] = "read_fault";
            bool ok = darklord_read(slot, 0, definition) && darklord_decl(definition, decl);
            ok &= darklord_read(definition, 0x158, parent) && darklord_decl(parent, inherit);
            darklord_emit(*call, "slot", "slot=%d decl=%s inherit=%s read_fault=%u",
                i, decl, inherit, ok ? 0u : 1u);
            call->slots.push_back({definition, i});
        }
    } catch (...) { darklord_emit(*call, "read_fault", "field=slot_telemetry"); }
}
void darklord_call_start(DarklordCall* call) noexcept {
    if (!call) return;
    darklord_read(call->world, 0x9a038, call->rng_call);
    call->call_at = GetTickCount64(); call->call_start = CostClock::now();
}
void darklord_end(DarklordCall* call, bool abnormal, uint64_t duration) noexcept {
    if (!call) return;
    try {
        uint32_t rng = 0;
        std::array<uint32_t, 64> after{};
        int32_t count = -1;
        const bool ok = darklord_handles(call->component, after, count);
        const bool rng_ok = darklord_read(call->world, 0x9a038, rng);
        call->game_tick = darklord_game_tick(call->world);
        darklord_emit(*call, "end", "path=managed_boss rng_before=0x%x rng_call_before=0x%x rng_after=0x%x count_before=%d count_after=%d original_us=%llu abnormal=%u read_fault=%u",
            call->rng, call->rng_call, rng, call->before_count, count, duration, abnormal ? 1u : 0u,
            ok && rng_ok && call->before_ok ? 0u : 1u);
        if (!ok || !call->before_ok) { darklord_emit(*call, "read_fault", "field=handle_diff"); return; }
        for (int32_t i = 0; i < count; ++i) {
            if (std::find(call->before.begin(), call->before.begin() + call->before_count, after[i]) != call->before.begin() + call->before_count) continue;
            darklord_track(*call, after[i]);
        }
    } catch (...) { darklord_emit(*call, "read_fault", "field=actor_telemetry"); }
}
void darklord_watch(std::unique_ptr<DarklordCall> call) noexcept {
    if (!call || call->actors.empty()) return;
    try { const auto seq = call->seq; darklord_calls.emplace(seq, std::move(call)); }
    catch (...) { if (call) darklord_emit(*call, "read_fault", "field=sampler_allocation"); }
}
void darklord_sample(uintptr_t world, uint64_t now) noexcept {
    try {
        for (auto call = darklord_calls.begin(); call != darklord_calls.end();) {
            auto& value = *call->second;
            uintptr_t boss = 0;
            const bool active = world == value.world && darklord_read(value.component, 8, boss) &&
                boss && boss == value.boss && darklord_map(world);
            for (auto& actor : value.actors) {
                if (actor.finished || (active && now - actor.created < darklord_sample_ms[actor.next])) continue;
                if (!active) {
                    darklord_actor_line(value, actor, "summary", "component_inactive", now); actor.finished = true; continue;
                }
                const auto elapsed = now - actor.created;
                uint64_t due = darklord_sample_ms[actor.next++];
                while (actor.next < std::size(darklord_sample_ms) && darklord_sample_ms[actor.next] <= elapsed) {
                    due = darklord_sample_ms[actor.next++]; ++actor.skipped;
                }
                const auto next = darklord_state(value.world, actor.handle, actor.last.actor);
                const bool moved = !next.fault && !next.removed && darklord_distance(next, actor.last) >= 0.02f;
                const bool state_changed = !darklord_same_state(next, actor.last);
                if (moved && !actor.recovered) actor.recovered = elapsed;
                if (!moved && !state_changed && !next.fault && !next.removed && !std::strcmp(next.anim_read, "ok"))
                    actor.frozen += now - actor.sampled;
                if (next.fault || std::strcmp(next.anim_read, "ok")) actor.state_unknown += now - actor.sampled;
                const bool changed = moved || state_changed || next.health != actor.last.health || next.armor != actor.last.armor;
                actor.last = next; actor.sampled = now; ++actor.samples;
                actor.finished = (!next.fault && (next.removed || (next.flags & 0x10000))) || actor.next == std::size(darklord_sample_ms);
                value.game_tick = darklord_game_tick(value.world);
                if (changed || actor.finished) darklord_actor_line(value, actor, actor.finished ? "summary" : "sample",
                    next.fault ? "read_fault" : next.removed ? "removed" : (next.flags & 0x10000) ? "dead" : actor.finished ? "window_complete" : "changed", now, due);
            }
            if (std::all_of(value.actors.begin(), value.actors.end(), [](const auto& actor) { return actor.finished; }))
                call = darklord_calls.erase(call);
            else ++call;
        }
    } catch (...) {}
}
void darklord_clear() noexcept {
    for (auto& [seq, call] : darklord_calls) {
        (void)seq;
        for (auto& actor : call->actors) if (!actor.finished)
            darklord_actor_line(*call, actor, "summary", "map_freed", GetTickCount64());
    }
    darklord_calls.clear(); ++darklord_epoch;
}
