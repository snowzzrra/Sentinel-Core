#pragma once

// Included in special_native.cpp's private namespace. The engine owns all clips.
namespace hud {
struct Value { uint32_t type, reserved; uintptr_t payload; };
struct alignas(8) String { unsigned char storage[0x30]; };
struct Calls {
    Value* (*lookup)(uintptr_t, Value*, const char*) = nullptr;
    uintptr_t (*sprite)(const Value*) = nullptr;
    uintptr_t (*text)(const Value*) = nullptr;
    void (*release)(Value*) = nullptr;
    void (*string_init)(String*) = nullptr;
    void (*string_set)(String*, const char*) = nullptr;
    void (*string_free)(String*) = nullptr;
    uintptr_t (*duplicate)(uintptr_t, const String*) = nullptr;
    uintptr_t (*entry)(uintptr_t, int) = nullptr;
    uintptr_t (*add)(uintptr_t, int, unsigned, const String*) = nullptr;
    void (*dirty)(uintptr_t) = nullptr;
    void (*start)(uintptr_t, int) = nullptr;
    void (*frame)(uintptr_t, int) = nullptr;
    void (*visible)(uintptr_t, bool, bool) = nullptr;
    void (*set_text)(uintptr_t, const char*) = nullptr;
    void (*position)(uintptr_t, float, float) = nullptr;
    void (*color)(uintptr_t, int) = nullptr;
    void (*material)(uintptr_t, uintptr_t, int, int, unsigned) = nullptr;
    uintptr_t (*find_material)(uintptr_t, const char*, int) = nullptr;
} swf;
bool graphics_ready = false, keycap_ready = false;

void refuse(const char* reason, uintptr_t owner, uintptr_t element, uint64_t epoch) {
    hud_trace.record(save::BStage::profile_output, save::BStatus::refused,
        reason, 0, {{"owner", owner}, {"element", element}, {"generation", epoch}});
}

uintptr_t child(uintptr_t parent, const char* name) {
    if (!parent) return 0;
    Value value{};
    const auto result = swf.lookup(parent, &value, name);
    const auto object = swf.sprite(result);
    if (value.type == 2 || value.type == 8) swf.release(&value);
    return object;
}

uintptr_t text_child(uintptr_t parent, const char* name) {
    if (!parent) return 0;
    Value value{};
    const auto result = swf.lookup(parent, &value, name);
    const auto object = swf.text(result);
    if (value.type == 2 || value.type == 8) swf.release(&value);
    return object;
}

// idSWFTextInstance::text is idStr at +0x40 (the native setter passes this
// address to idStr::operator=). Its data and length are at +0x8/+0x10.
bool text_is(uintptr_t value, const char* label) {
    const auto length = std::strlen(label);
    const auto data = *reinterpret_cast<const char* const*>(value + 0x48);
    return *reinterpret_cast<int32_t*>(value + 0x50) == static_cast<int32_t>(length) && data &&
        std::memcmp(data, label, length) == 0;
}

struct Point { float x, y; };
bool position(uintptr_t sprite, Point& out) {
    if (!sprite || !*reinterpret_cast<uintptr_t*>(sprite + 0x40)) return false;
    const auto slot = *reinterpret_cast<int32_t*>(sprite + 0xc);
    const auto context = *reinterpret_cast<uintptr_t*>(sprite + 0x10);
    if (slot < 0 || !context) return false;
    const auto transforms = *reinterpret_cast<uintptr_t*>(context + 0x80);
    if (!transforms) return false;
    const auto transform = transforms + static_cast<uintptr_t>(slot) * 0x40;
    out = {*reinterpret_cast<float*>(transform + 0x14), *reinterpret_cast<float*>(transform + 0x18)};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

void show(uintptr_t clip, bool visible) {
    if (*reinterpret_cast<uint8_t*>(clip + 0x51) != static_cast<uint8_t>(visible))
        swf.visible(clip, visible, true);
}

void place(uintptr_t clip, Point at) {
    Point current{};
    if (!position(clip, current) || current.x != at.x || current.y != at.y)
        swf.position(clip, at.x, at.y);
}

bool hold_frame(uintptr_t clip, uint16_t frame) {
    if (*reinterpret_cast<uint16_t*>(clip + 0x58) != frame ||
        *reinterpret_cast<uint8_t*>(clip + 0x50) ||
        *reinterpret_cast<uint16_t*>(clip + 0x5c)) {
        swf.frame(clip, frame);
        return true;
    }
    return false;
}

bool key_offset(uintptr_t clip, uintptr_t source, uintptr_t movie, Point& out) {
    // The 0x40 transform slot contains swfMatrix_t after its 4-byte header:
    // xx, yy, xy, yx, tx, ty. swf.position writes tx/ty at +0x14/+0x18.
    Point point{};
    for (unsigned i = 0; i < 4 && clip; ++i) {
        if (*reinterpret_cast<uintptr_t*>(clip + 0x30) != movie) return false;
        const auto slot = *reinterpret_cast<int32_t*>(clip + 0xc);
        const auto context = *reinterpret_cast<uintptr_t*>(clip + 0x10);
        if (slot < 0 || !context) return false;
        const auto transforms = *reinterpret_cast<uintptr_t*>(context + 0x80);
        if (!transforms) return false;
        const auto t = transforms + static_cast<uintptr_t>(slot) * 0x40;
        const Point next{
            *reinterpret_cast<float*>(t + 0x4) * point.x +
                *reinterpret_cast<float*>(t + 0xc) * point.y + *reinterpret_cast<float*>(t + 0x14),
            *reinterpret_cast<float*>(t + 0x10) * point.x +
                *reinterpret_cast<float*>(t + 0x8) * point.y + *reinterpret_cast<float*>(t + 0x18)};
        if (!std::isfinite(next.x) || !std::isfinite(next.y)) return false;
        point = next;
        if (clip == source) {
            Point anchor{};
            if (!position(source, anchor)) return false;
            out = {point.x - anchor.x, point.y - anchor.y};
            return true;
        }
        clip = *reinterpret_cast<uintptr_t*>(clip + 0x40);
    }
    return false;
}

bool key_name(unsigned vk, char (&name)[16]) {
    if (!vk) return false;
    if (vk >= VK_F1 && vk <= VK_F12) {
        std::snprintf(name, sizeof(name), "F%u", vk - VK_F1 + 1);
        return true;
    }
    const auto scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    return scan && GetKeyNameTextA(static_cast<LONG>(scan << 16), name, sizeof(name)) > 0;
}

// Clone native sprites under their live SWF parent, without copying objects.
uintptr_t clone(uintptr_t source, uintptr_t parent, const char* name, bool& created) {
    if (const auto found = child(parent, name)) return found;
    if (!source || !parent) return 0;
    const auto source_parent = *reinterpret_cast<uintptr_t*>(source + 0x40);
    if (!source_parent || *reinterpret_cast<uintptr_t*>(source + 0x30) !=
                          *reinterpret_cast<uintptr_t*>(parent + 0x30)) return 0;
    String native_name{};
    swf.string_init(&native_name);
    uintptr_t result = 0;
    __try {
        swf.string_set(&native_name, name);
        if (source_parent == parent) result = swf.duplicate(source, &native_name);
        else {
            const auto entry = swf.entry(source_parent, *reinterpret_cast<int32_t*>(source + 0x48));
            if (entry && *reinterpret_cast<uintptr_t*>(entry + 0x30) == source) {
                const auto depth = *reinterpret_cast<int32_t*>(parent + 0x78) + 0x4001;
                const auto added = swf.add(parent, depth, *reinterpret_cast<uint16_t*>(entry), &native_name);
                result = added ? *reinterpret_cast<uintptr_t*>(added + 0x30) : 0;
                if (result) { swf.dirty(result); swf.start(result, 1); }
            }
        }
    } __finally { swf.string_free(&native_name); }
    if (result) show(result, false);
    created |= result != 0;
    return result;
}

struct GraphicsSource {
    uintptr_t widget = 0, hammer = 0, quickuse = 0, secondary = 0;
    uintptr_t primary_root = 0, secondary_root = 0, crucible_root = 0, hammer_root = 0;
    uintptr_t parent = 0, movie = 0, source = 0;
    bool crucible_movie_match = false, hammer_movie_match = false;
};

GraphicsSource graphics_source(uintptr_t element) {
    GraphicsSource s{};
    s.widget = *reinterpret_cast<uintptr_t*>(element + 0x1e8);
    s.hammer = *reinterpret_cast<uintptr_t*>(element + 0x1f8);
    s.quickuse = *reinterpret_cast<uintptr_t*>(element + 0x1d0);
    s.secondary = *reinterpret_cast<uintptr_t*>(element + 0x1d8);
    s.primary_root = s.quickuse ? *reinterpret_cast<uintptr_t*>(s.quickuse + 0x18) : 0;
    s.secondary_root = s.secondary ? *reinterpret_cast<uintptr_t*>(s.secondary + 0x18) : 0;
    s.crucible_root = s.widget ? *reinterpret_cast<uintptr_t*>(s.widget + 0x18) : 0;
    s.hammer_root = s.hammer ? *reinterpret_cast<uintptr_t*>(s.hammer + 0x18) : 0;
    s.parent = s.primary_root ? *reinterpret_cast<uintptr_t*>(s.primary_root + 0x40) : 0;
    s.movie = s.parent ? *reinterpret_cast<uintptr_t*>(s.parent + 0x30) : 0;
    if (!s.movie || !s.secondary_root ||
        *reinterpret_cast<uintptr_t*>(s.primary_root + 0x30) != s.movie ||
        *reinterpret_cast<uintptr_t*>(s.secondary_root + 0x30) != s.movie ||
        *reinterpret_cast<uintptr_t*>(s.secondary_root + 0x40) != s.parent) return s;
    const auto matches = [&](uintptr_t root) {
        if (!root || *reinterpret_cast<uintptr_t*>(root + 0x30) != s.movie) return false;
        const auto root_parent = *reinterpret_cast<uintptr_t*>(root + 0x40);
        return root_parent && *reinterpret_cast<uintptr_t*>(root_parent + 0x30) == s.movie;
    };
    s.crucible_movie_match = matches(s.crucible_root);
    s.hammer_movie_match = matches(s.hammer_root);
    if (s.crucible_movie_match && child(s.crucible_root, "icon") && child(s.crucible_root, "pips"))
        s.source = s.crucible_root;
    else if (s.hammer_movie_match && child(s.hammer_root, "icon") && child(s.hammer_root, "pips"))
        s.source = s.hammer_root;
    return s;
}

bool project(uintptr_t element, const HudOwnerSnapshot& owner, const GraphicsSource& context,
             unsigned keys, uint64_t epoch, bool& clips_applied) {
    clips_applied = false;
    // Avoid concurrent or reentrant mutations of the same SWF tree without
    // waiting inside unknown engine callbacks.
    static std::atomic_flag presenting = ATOMIC_FLAG_INIT;
    if (presenting.test_and_set(std::memory_order_acquire)) return false;
    struct ReleasePresentation {
        std::atomic_flag& flag;
        ~ReleasePresentation() { flag.clear(std::memory_order_release); }
    } release{presenting};
    if (!graphics_ready) {
        refuse("hud_graphics_unavailable", element, 0, epoch);
        return false;
    }
    static std::mutex state_mutex;
    const auto source = context.source, parent = context.parent, movie = context.movie;
    const auto primary_root = context.primary_root, secondary_root = context.secondary_root;
    const auto crucible_root = context.crucible_root, quickuse = context.quickuse;
    const auto widget = context.widget, hammer = context.hammer;
    struct Refusal { uintptr_t element, widget, hammer, primary, secondary, parent, movie; uint64_t epoch; };
    static Refusal last{};
    if (!source) {
        if (parent && movie && primary_root && secondary_root &&
            *reinterpret_cast<uintptr_t*>(parent + 0x30) == movie &&
            *reinterpret_cast<uintptr_t*>(primary_root + 0x30) == movie &&
            *reinterpret_cast<uintptr_t*>(primary_root + 0x40) == parent &&
            *reinterpret_cast<uintptr_t*>(secondary_root + 0x30) == movie &&
            *reinterpret_cast<uintptr_t*>(secondary_root + 0x40) == parent) {
            for (const auto name : {"apAmmoRefill", "apSpecialSwitch",
                                    "apAmmoRefillBind", "apSpecialToggleBind"})
                if (const auto clip = child(parent, name)) show(clip, false);
        }
        const Refusal current{element, widget, hammer, primary_root, secondary_root, parent, movie, epoch};
        {
            std::lock_guard lock(state_mutex);
            if (last.element == current.element && last.widget == current.widget &&
                last.hammer == current.hammer && last.primary == current.primary &&
                last.secondary == current.secondary && last.parent == current.parent &&
                last.movie == current.movie && last.epoch == current.epoch) return false;
            last = current;
        }
        hud_trace.record(save::BStage::profile_output, save::BStatus::refused,
            "hud_graphics_source_unavailable", 0,
            {{"owner", element}, {"widget", widget}, {"hammer", hammer},
             {"quickuse", quickuse}, {"secondary", context.secondary},
             {"primary_root", primary_root}, {"secondary_root", secondary_root},
             {"parent", parent}, {"movie", movie}, {"crucible_root", crucible_root},
             {"crucible_movie_match", context.crucible_movie_match},
             {"hammer_root", context.hammer_root},
             {"hammer_movie_match", context.hammer_movie_match}, {"generation", epoch}});
        return false;
    }
    {
        std::lock_guard lock(state_mutex);
        last = {};
    }
    struct RenderState {
        uintptr_t element = 0, parent = 0, movie = 0, source = 0, primary = 0, secondary = 0;
        uint64_t epoch = 0, revision = 0, request_revision = 0;
        uint32_t balance = UINT32_MAX, flags = 0, request_state = 0, selected = 0;
        uint32_t owns_crucible = 0, owns_hammer = 0;
        unsigned keys = 0;
        bool namespace_valid = false, keycaps = false, element_visible = false, presented = false;
        bool graphics_applied = false, fully_applied = false;
        uint64_t retry_at = 0;
    };
    static RenderState shared_rendered{};
    RenderState rendered;
    {
        std::lock_guard lock(state_mutex);
        rendered = shared_rendered;
    }
    const auto save_render = [&](RenderState state) {
        std::lock_guard lock(state_mutex);
        shared_rendered = state;
    };
    const RenderState current{element, parent, movie, source, primary_root, secondary_root,
        epoch, owner.revision, owner.request_revision, owner.refill_balance, owner.refill_flags,
        owner.refill_request_state, owner.selected, owner.owns_crucible, owner.owns_hammer,
        keys, owner.namespace_valid, keycap_ready,
        *reinterpret_cast<uint8_t*>(element + 0x209) != 0};
    const bool same = rendered.element == current.element && rendered.parent == current.parent &&
        rendered.movie == current.movie && rendered.source == current.source &&
        rendered.primary == current.primary && rendered.secondary == current.secondary &&
        rendered.epoch == current.epoch && rendered.revision == current.revision &&
        rendered.request_revision == current.request_revision && rendered.balance == current.balance &&
        rendered.flags == current.flags && rendered.request_state == current.request_state &&
        rendered.selected == current.selected && rendered.owns_crucible == current.owns_crucible &&
        rendered.owns_hammer == current.owns_hammer && rendered.namespace_valid == current.namespace_valid &&
        rendered.keys == current.keys &&
        rendered.keycaps == current.keycaps && rendered.element_visible == current.element_visible;
    if (same && rendered.presented && !current.element_visible) {
        const auto hidden = [&](const char* name) {
            const auto clip = child(parent, name);
            return !clip || !*reinterpret_cast<uint8_t*>(clip + 0x51);
        };
        if (hidden("apAmmoRefill") && hidden("apSpecialSwitch") &&
            hidden("apAmmoRefillBind") && hidden("apSpecialToggleBind")) return false;
    }
    Point anchor{}, adjacent{};
    if (!position(primary_root, anchor) || !position(secondary_root, adjacent)) {
        refuse("hud_swf_context_unavailable", element, source, epoch); return false;
    }
    auto refill = child(parent, "apAmmoRefill");
    auto special_arrow = child(parent, "apSpecialSwitch");
    auto refill_bind = child(parent, "apAmmoRefillBind");
    auto special_bind = child(parent, "apSpecialToggleBind");
    const bool visible = owner.namespace_valid && *reinterpret_cast<uint8_t*>(element + 0x209);
    // All references come from this update's live parent, including invalidation.
    if (!visible) {
        if (refill) show(refill, false);
        if (special_arrow) show(special_arrow, false);
        if (refill_bind) show(refill_bind, false);
        if (special_bind) show(special_bind, false);
        auto state = current;
        state.presented = true;
        save_render(state);
        hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded,
            "hud_invalidated", 0, {{"owner", element}, {"parent", parent}, {"generation", epoch},
                                   {"snapshot_valid", owner.namespace_valid}, {"context_valid", 1}});
        return false;
    }
    const Point step{anchor.x - adjacent.x, anchor.y - adjacent.y};
    if (step.x * step.x + step.y * step.y < 1.0f) {
        refuse("hud_slot_spacing_unknown", element, source, epoch); return false;
    }
    const Point refill_at{anchor.x + step.x, anchor.y + step.y};
    const Point offset{-step.y * 0.35f, step.x * 0.35f};
    bool created = false;
    const bool switch_available = owner.owns_crucible && owner.owns_hammer &&
        route_ready.load(std::memory_order_acquire) && ((keys >> 8) & 0xff);
    const auto arrow_source = switch_available ? child(parent, "swapEquipment") : 0;
    const char* switch_failure = switch_available && !arrow_source ? "switch_source_missing" : nullptr;
    if (switch_available && !special_arrow)
        special_arrow = clone(arrow_source, parent, "apSpecialSwitch", created);
    if (switch_available && !special_arrow && !switch_failure)
        switch_failure = "switch_clone_failed";
    if (switch_available && special_arrow) {
        hold_frame(special_arrow, 1);
        if (const auto cta = child(special_arrow, "cta")) show(cta, false);
        if (const auto cta = child(child(special_arrow, "icon"), "cta")) show(cta, false);
        place(special_arrow, {anchor.x + offset.x, anchor.y + offset.y});
        show(special_arrow, true);
    } else if (special_arrow) show(special_arrow, false);
    const bool new_refill = !refill;
    if (!refill) refill = clone(source, parent, "apAmmoRefill", created);
    if (!refill) { refuse("hud_refill_clone_failed", element, source, epoch); return false; }
    const auto fail_refill = [&](const char* reason, uintptr_t detail) {
        show(refill, false);
        if (refill_bind) show(refill_bind, false);
        refuse(reason, element, detail, epoch);
        return false;
    };
    hud_trace.record(save::BStage::profile_prepare, save::BStatus::succeeded,
        "hud_bound", 0, {{"owner", element}, {"parent", parent}, {"layout_source", source},
                         {"generation", epoch}, {"created", created}, {"refill", refill}});
    const bool known = owner.refill_balance <= 3;
    hold_frame(refill, owner.refill_enabled ? 1 : 2);
    // The equipment clone carries its donor CTA; the AP labels are separate clips.
    if (const auto cta = child(child(refill, "icon"), "cta")) show(cta, false);
    const auto icon = child(child(refill, "icon"), "iconStatic");
    if (!icon) return fail_refill("hud_refill_icon_absent", refill);
    struct MaterialAttempt {
        uintptr_t owner, parent, clip, icon, material;
        uint64_t epoch, retry_at;
        bool applied;
    };
    static MaterialAttempt shared_attempt{};
    MaterialAttempt attempt;
    {
        std::lock_guard lock(state_mutex);
        attempt = shared_attempt;
    }
    if (new_refill || attempt.owner != element || attempt.parent != parent || attempt.clip != refill ||
        attempt.icon != icon || attempt.epoch != epoch)
        attempt = {element, parent, refill, icon, 0, epoch, 0, false};
    if (attempt.applied && *reinterpret_cast<uintptr_t*>(icon + 0x60) != attempt.material) {
        attempt.applied = false;
        attempt.retry_at = 0;
    }
    if (!attempt.applied && GetTickCount64() >= attempt.retry_at) {
        attempt.retry_at = GetTickCount64() + 1000;
        const auto material = swf.find_material(image_base + 0x5e05200, "art/ui/icons/ammo/bullets", 0);
        if (material) {
            swf.material(icon, material, -1, -1, 0);
            attempt.material = material;
            attempt.applied = *reinterpret_cast<uintptr_t*>(icon + 0x60) == material;
        }
    }
    {
        std::lock_guard lock(state_mutex);
        shared_attempt = attempt;
    }
    if (!attempt.applied) {
        return fail_refill("hud_ammo_material_unavailable", icon);
    }
    const auto pips = child(refill, "pips");
    if (!pips) return fail_refill("hud_pips_absent", refill);
    hold_frame(pips, 3);
    const auto three = child(pips, "pips3");
    if (!three) return fail_refill("hud_three_pip_template_absent", pips);
    show(pips, known);
    const auto desired = static_cast<uint16_t>(owner.refill_balance + 1);
    if (known && hold_frame(three, desired)) swf.dirty(three);
    const auto palette = *reinterpret_cast<int32_t*>((source == crucible_root ? widget : hammer) + 0x1ec);
    if (const auto fill = child(three, "fill")) swf.color(fill, palette);
    if (const auto fill = child(three, "innerFill")) swf.color(fill, palette);
    place(refill, refill_at);
    show(refill, true);
    const bool graphics_applied = !known || *reinterpret_cast<uint16_t*>(three + 0x58) == desired;
    if (!keycap_ready) {
        if (refill_bind) show(refill_bind, false);
        if (special_bind) show(special_bind, false);
        auto state = current;
        state.presented = true;
        state.graphics_applied = graphics_applied;
        save_render(state);
        hud_trace.record(save::BStage::profile_output, save::BStatus::pending,
            "hud_graphics_applied_keycaps_unavailable", 0,
            {{"owner", element}, {"parent", parent}, {"graphics_ready", graphics_ready},
             {"keycap_ready", keycap_ready}});
        return graphics_applied;
    }
    const auto donor_root = primary_root;
    const auto donor = quickuse ? *reinterpret_cast<uintptr_t*>(quickuse + 0x1f0) : 0;
    Point donor_offset{};
    if (!donor_root || *reinterpret_cast<uintptr_t*>(donor_root + 0x40) != parent ||
        !donor || !key_offset(donor, donor_root, movie, donor_offset) ||
        !child(donor, "kbm") || !child(donor, "joy")) {
        if (refill_bind) show(refill_bind, false);
        if (special_bind) show(special_bind, false);
        auto state = current;
        state.presented = true;
        state.graphics_applied = graphics_applied;
        state.retry_at = GetTickCount64() + 1000;
        save_render(state);
        refuse("hud_native_keycap_donor_unavailable", element, donor, epoch);
        return graphics_applied;
    }
    const char* bind_failure = nullptr;
    const auto bind_key = [&](uintptr_t& clip, const char* name, unsigned vk,
                              Point at, const char* missing_clip, const char* missing_text) {
        char label[16]{};
        if (!key_name(vk, label)) {
            if (clip) show(clip, false);
            if (vk) bind_failure = missing_text;
            return vk == 0;
        }
        if (!clip) clip = clone(donor, parent, name, created);
        if (!clip) { bind_failure = missing_clip; return false; }
        hold_frame(clip, 1);
        const auto kbm = child(clip, "kbm");
        const auto joy = child(clip, "joy");
        const auto value = text_child(kbm, "txtVal");
        if (!kbm || !joy || !value) {
            show(clip, false);
            bind_failure = value ? missing_clip : missing_text;
            return false;
        }
        hold_frame(kbm, 1);
        hold_frame(joy, 1);
        if (!text_is(value, label)) swf.set_text(value, label);
        if (!text_is(value, label)) {
            show(clip, false);
            bind_failure = missing_text;
            return false;
        }
        show(joy, false);
        show(kbm, true);
        place(clip, {at.x + donor_offset.x, at.y + donor_offset.y});
        show(clip, true);
        return true;
    };
    const auto ammo_key = keys & 0xff;
    const auto toggle_key = switch_available && special_arrow ? (keys >> 8) & 0xff : 0;
    if (!bind_key(refill_bind, "apAmmoRefillBind", ammo_key, refill_at,
                  "ammo_keycap_missing", "ammo_text_failed") ||
        !bind_key(special_bind, "apSpecialToggleBind", toggle_key,
                  {anchor.x + offset.x, anchor.y + offset.y},
                  "toggle_keycap_missing", "toggle_text_failed")) {
        auto state = current;
        state.presented = true;
        state.graphics_applied = graphics_applied;
        state.retry_at = GetTickCount64() + 1000;
        save_render(state);
        refuse(bind_failure ? bind_failure : "hud_native_keycap_bind_failed", element, donor, epoch);
        return graphics_applied;
    }
    if (switch_failure) {
        auto state = current;
        state.presented = true;
        state.graphics_applied = graphics_applied;
        save_render(state);
        refuse(switch_failure, element, arrow_source, epoch);
        return graphics_applied;
    }
    auto state = current;
    state.presented = true;
    state.graphics_applied = graphics_applied;
    state.fully_applied = graphics_applied;
    save_render(state);
    clips_applied = graphics_applied;
    hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded,
        "hud_native_keycaps_applied", 0,
        {{"owner", element}, {"parent", parent}, {"movie", movie}, {"generation", epoch},
         {"created", created}, {"balance", owner.refill_balance}, {"binds", keys},
         {"snapshot_revision", owner.revision},
         {"pip_frame_after", *reinterpret_cast<uint16_t*>(three + 0x58)},
         {"refill_clip", refill}, {"switch_clip", special_arrow},
         {"refill_bind_clip", refill_bind}, {"toggle_bind_clip", special_bind},
         {"pixels_observed", 0}});
    return graphics_applied;
}
} // namespace hud
