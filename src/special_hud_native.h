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

bool key_offset(uintptr_t clip, uintptr_t source, uintptr_t movie, Point& out) {
    out = {};
    for (unsigned i = 0; i < 3 && clip && clip != source; ++i) {
        if (*reinterpret_cast<uintptr_t*>(clip + 0x30) != movie) return false;
        Point local{};
        if (!position(clip, local)) return false;
        out.x += local.x;
        out.y += local.y;
        clip = *reinterpret_cast<uintptr_t*>(clip + 0x40);
    }
    return clip == source;
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
    if (result) swf.visible(result, false, true);
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

bool project(uintptr_t element, const HudOwnerSnapshot& owner, bool valid, unsigned keys, uint64_t epoch) {
    if (!graphics_ready) {
        refuse("hud_graphics_unavailable", element, 0, epoch);
        return false;
    }
    if (!valid) return false;
    const auto context = graphics_source(element);
    const auto source = context.source, parent = context.parent, movie = context.movie;
    const auto primary_root = context.primary_root, secondary_root = context.secondary_root;
    const auto crucible_root = context.crucible_root, quickuse = context.quickuse;
    const auto widget = context.widget, hammer = context.hammer;
    if (!source) {
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
    if (refill) swf.visible(refill, false, true);
    if (special_arrow) swf.visible(special_arrow, false, true);
    if (refill_bind) swf.visible(refill_bind, false, true);
    if (special_bind) swf.visible(special_bind, false, true);
    if (!visible) {
        hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded,
            "hud_invalidated", 0, {{"owner", element}, {"parent", parent}, {"generation", epoch},
                                   {"snapshot_valid", owner.namespace_valid}, {"context_valid", valid}});
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
    if (switch_available && !special_arrow)
        special_arrow = clone(arrow_source, parent, "apSpecialSwitch", created);
    if (switch_available && special_arrow) {
        swf.position(special_arrow, anchor.x + offset.x, anchor.y + offset.y);
        swf.visible(special_arrow, true, true);
    }
    const bool new_refill = !refill;
    if (!refill) refill = clone(source, parent, "apAmmoRefill", created);
    if (!refill) { refuse("hud_refill_clone_failed", element, source, epoch); return false; }
    hud_trace.record(save::BStage::profile_prepare, save::BStatus::succeeded,
        "hud_bound", 0, {{"owner", element}, {"parent", parent}, {"layout_source", source},
                         {"generation", epoch}, {"created", created}, {"refill", refill}});
    const bool known = owner.refill_balance <= 3;
    const bool pending = owner.refill_request_state == SC_SPECIAL_REFILL_PENDING;
    const bool connected = (owner.refill_flags & SC_SPECIAL_REFILL_CONNECTED) != 0;
    swf.frame(refill, owner.refill_enabled ? 1 : 2);
    // The equipment clone carries its donor CTA; the AP labels are separate clips.
    if (const auto cta = child(child(refill, "icon"), "cta")) swf.visible(cta, false, true);
    const auto icon = child(child(refill, "icon"), "iconStatic");
    if (!icon) { refuse("hud_refill_icon_absent", element, refill, epoch); return false; }
    struct MaterialAttempt {
        uintptr_t owner, parent, clip, icon;
        uint64_t epoch, retry_at;
        bool applied;
    };
    thread_local MaterialAttempt attempt{};
    if (new_refill || attempt.owner != element || attempt.parent != parent || attempt.clip != refill ||
        attempt.icon != icon || attempt.epoch != epoch)
        attempt = {element, parent, refill, icon, epoch, 0, false};
    if (attempt.applied && !*reinterpret_cast<uintptr_t*>(icon + 0x60)) {
        attempt.applied = false;
        attempt.retry_at = 0;
    }
    if (!attempt.applied && GetTickCount64() >= attempt.retry_at) {
        attempt.retry_at = GetTickCount64() + 1000;
        const auto material = swf.find_material(image_base + 0x5e05200, "art/ui/icons/ammo/bullets", 0);
        if (material) {
            swf.material(icon, material, -1, -1, 0);
            attempt.applied = *reinterpret_cast<uintptr_t*>(icon + 0x60) == material;
        }
    }
    if (!attempt.applied) {
        refuse("hud_ammo_material_unavailable", element, icon, epoch); return false;
    }
    const auto pips = child(refill, "pips");
    if (!pips) { refuse("hud_pips_absent", element, refill, epoch); return false; }
    swf.frame(pips, 3);
    const auto three = child(pips, "pips3");
    if (!three) { refuse("hud_three_pip_template_absent", element, pips, epoch); return false; }
    swf.visible(pips, known, true);
    const auto frame_before = *reinterpret_cast<uint16_t*>(three + 0x58);
    const auto desired = static_cast<uint16_t>(owner.refill_balance + 1);
    if (known) {
        if (*reinterpret_cast<uint16_t*>(three + 0x58) != desired) {
            swf.frame(three, desired);
            swf.dirty(three);
        }
    }
    const auto palette = *reinterpret_cast<int32_t*>((source == crucible_root ? widget : hammer) + 0x1ec);
    if (const auto fill = child(three, "fill")) swf.color(fill, palette);
    if (const auto fill = child(three, "innerFill")) swf.color(fill, palette);
    swf.position(refill, refill_at.x, refill_at.y);
    swf.visible(refill, true, true);
    const bool graphics_applied = known && *reinterpret_cast<uint16_t*>(three + 0x58) == desired;
    if (!keycap_ready) {
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
        refuse("hud_native_keycap_donor_unavailable", element, donor, epoch);
        return graphics_applied;
    }
    struct BindState {
        uintptr_t parent = 0, refill = 0, special = 0;
        uint64_t epoch = 0;
        unsigned ammo_key = 0, toggle_key = 0;
    };
    thread_local BindState binds{};
    const auto bind_key = [&](uintptr_t& clip, const char* name, unsigned vk,
                              Point at, unsigned& cached_key) {
        char label[16]{};
        if (!key_name(vk, label)) return true;
        if (!clip) clip = clone(donor, parent, name, created);
        const auto kbm = child(clip, "kbm");
        const auto joy = child(clip, "joy");
        const auto value = text_child(kbm, "txtVal");
        if (!clip || !kbm || !joy || !value) return false;
        if (cached_key != vk) swf.set_text(value, label);
        cached_key = vk;
        swf.visible(joy, false, true);
        swf.visible(kbm, true, true);
        swf.position(clip, at.x + donor_offset.x, at.y + donor_offset.y);
        swf.visible(clip, true, true);
        return true;
    };
    if (binds.parent != parent || binds.refill != refill_bind ||
        binds.special != special_bind || binds.epoch != epoch)
        binds = {parent, refill_bind, special_bind, epoch};
    const auto ammo_key = keys & 0xff;
    const auto toggle_key = switch_available && special_arrow ? (keys >> 8) & 0xff : 0;
    if (!bind_key(refill_bind, "apAmmoRefillBind", ammo_key, refill_at, binds.ammo_key) ||
        !bind_key(special_bind, "apSpecialToggleBind", toggle_key,
                  {anchor.x + offset.x, anchor.y + offset.y}, binds.toggle_key)) {
        refuse("hud_native_keycap_bind_failed", element, donor, epoch);
        return graphics_applied;
    }
    binds.refill = refill_bind;
    binds.special = special_bind;
    hud_trace.record(save::BStage::profile_output, save::BStatus::pending,
        "hud_native_keycaps_applied", 0,
        {{"owner", element}, {"parent", parent}, {"generation", epoch}, {"created", created},
         {"snapshot_valid", owner.namespace_valid}, {"policy", owner.selected}, {"balance", owner.refill_balance},
         {"pending", pending}, {"connected", connected}, {"binds", keys},
         {"snapshot_revision", owner.revision}, {"request_revision", owner.request_revision},
         {"pip_frame_before", frame_before}, {"pip_frame_after", *reinterpret_cast<uint16_t*>(three + 0x58)},
         {"native_crucible_170", *reinterpret_cast<int32_t*>(element + 0x170)}, {"pixels_observed", 0},
         {"switch_source", arrow_source}, {"switch_clip", special_arrow}});
    return graphics_applied;
}
} // namespace hud
