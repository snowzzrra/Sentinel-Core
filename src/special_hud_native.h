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
    void (*position)(uintptr_t, float, float) = nullptr;
    void (*color)(uintptr_t, int) = nullptr;
    void (*material)(uintptr_t, uintptr_t, int, int, unsigned) = nullptr;
    void (*set_text)(uintptr_t, const char*) = nullptr;
    uintptr_t (*cached)(uintptr_t) = nullptr;
    uintptr_t (*find_material)(uintptr_t, const char*, int) = nullptr;
} swf;
bool enabled = false;

void refuse(const char* reason, uintptr_t owner, uintptr_t element, uint64_t epoch) {
    hud_trace.record(save::BStage::profile_output, save::BStatus::refused,
        reason, 0, {{"owner", owner}, {"element", element}, {"generation", epoch}});
}

uintptr_t child(uintptr_t parent, const char* name, bool text = false) {
    if (!parent) return 0;
    Value value{};
    const auto result = swf.lookup(parent, &value, name);
    const auto object = text ? swf.text(result) : swf.sprite(result);
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

// A sibling uses DuplicateMovieClip. A text template from the same SWF uses
// its native definition and the real target parent, without copying objects.
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

void key_label(unsigned vk, char (&out)[32]) {
    if (!vk) std::snprintf(out, sizeof(out), "UNBOUND");
    else if (vk >= VK_F1 && vk <= VK_F24) std::snprintf(out, sizeof(out), "F%u", vk - VK_F1 + 1);
    else if (!GetKeyNameTextA(static_cast<LONG>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16), out, sizeof(out)))
        std::snprintf(out, sizeof(out), "VK%02X", vk);
}

bool label(uintptr_t clip, const char* value, Point at) {
    swf.frame(clip, 1);
    const auto text = child(clip, "txtVal", true);
    if (!text) return false;
    swf.set_text(text, value);
    swf.position(clip, at.x, at.y);
    return true;
}

void project(uintptr_t element, const HudOwnerSnapshot& owner, bool valid, unsigned keys, uint64_t epoch) {
    if (!enabled) return;
    const auto widget = *reinterpret_cast<uintptr_t*>(element + 0x1e8);
    const auto chainsaw = *reinterpret_cast<uintptr_t*>(element + 0x1e0);
    const auto source = widget ? *reinterpret_cast<uintptr_t*>(widget + 0x18) : 0;
    const auto neighbor = chainsaw ? *reinterpret_cast<uintptr_t*>(chainsaw + 0x18) : 0;
    const auto parent = source ? *reinterpret_cast<uintptr_t*>(source + 0x40) : 0;
    if (!parent) { refuse("hud_parent_absent", element, source, epoch); return; }
    auto refill = child(parent, "apAmmoRefill");
    auto refill_label = child(parent, "apRefillBind");
    auto special_label = child(parent, "apSpecialBind");
    auto special_arrow = child(parent, "apSpecialSwitch");
    const bool visible = valid && owner.namespace_valid && *reinterpret_cast<uint8_t*>(element + 0x209);
    // All references come from this update's live parent, including invalidation.
    if (refill) swf.visible(refill, false, true);
    if (refill_label) swf.visible(refill_label, false, true);
    if (special_label) swf.visible(special_label, false, true);
    if (special_arrow) swf.visible(special_arrow, false, true);
    if (!visible) {
        hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded,
            "hud_invalidated", 0, {{"owner", element}, {"parent", parent}, {"generation", epoch},
                                   {"snapshot_valid", owner.namespace_valid}, {"context_valid", valid}});
        return;
    }
    Point anchor{}, adjacent{};
    if (!position(source, anchor) || !position(neighbor, adjacent) ||
        *reinterpret_cast<uintptr_t*>(neighbor + 0x40) != parent) {
        refuse("hud_slot_anchor_unavailable", element, neighbor, epoch); return;
    }
    const Point step{anchor.x - adjacent.x, anchor.y - adjacent.y};
    if (step.x * step.x + step.y * step.y < 1.0f) {
        refuse("hud_slot_spacing_unknown", element, source, epoch); return;
    }
    const Point refill_at{anchor.x + step.x, anchor.y + step.y};
    bool created = false;
    const bool new_refill = !refill;
    if (!refill) refill = clone(source, parent, "apAmmoRefill", created);
    const auto equipped = swf.cached(element + 0x248);
    const auto main = child(child(equipped, "info"), "main");
    const auto template_text = child(main, "ammo");
    if (!refill_label) refill_label = clone(template_text, parent, "apRefillBind", created);
    if (!special_label) special_label = clone(template_text, parent, "apSpecialBind", created);
    if (!refill || !refill_label || !special_label) {
        refuse(!refill ? "hud_refill_clone_failed" : "hud_binding_text_template_unavailable",
               element, template_text, epoch); return;
    }
    const bool switch_available = owner.owns_crucible && owner.owns_hammer;
    const auto arrow_source = switch_available ? child(parent, "swapEquipment") : 0;
    if (switch_available && !special_arrow)
        special_arrow = clone(arrow_source, parent, "apSpecialSwitch", created);
    if (switch_available && !special_arrow) {
        refuse("hud_native_switch_template_unavailable", element, parent, epoch); return;
    }
    hud_trace.record(save::BStage::profile_prepare, save::BStatus::succeeded,
        "hud_bound", 0, {{"owner", element}, {"parent", parent}, {"layout_source", source},
                         {"generation", epoch}, {"created", created}, {"refill", refill}});
    const bool known = owner.refill_balance <= 3;
    const bool pending = owner.refill_request_state == SC_SPECIAL_REFILL_PENDING;
    const bool connected = (owner.refill_flags & SC_SPECIAL_REFILL_CONNECTED) != 0;
    swf.frame(refill, owner.refill_enabled ? 1 : 2);
    const auto icon = child(child(refill, "icon"), "iconStatic");
    if (!icon) { refuse("hud_refill_icon_absent", element, refill, epoch); return; }
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
        refuse("hud_ammo_material_unavailable", element, icon, epoch); return;
    }
    const auto pips = child(refill, "pips");
    if (!pips) { refuse("hud_pips_absent", element, refill, epoch); return; }
    swf.frame(pips, 3);
    const auto three = child(pips, "pips3");
    if (!three) { refuse("hud_three_pip_template_absent", element, pips, epoch); return; }
    swf.visible(pips, known, true);
    if (known) swf.frame(three, static_cast<int>(owner.refill_balance + 1));
    const auto palette = *reinterpret_cast<int32_t*>(widget + 0x1ec);
    if (const auto fill = child(three, "fill")) swf.color(fill, palette);
    if (const auto fill = child(three, "innerFill")) swf.color(fill, palette);
    char refill_key[32]{}, special_key[32]{}, refill_text[64]{}, special_text[64]{};
    key_label(keys & 0xff, refill_key);
    key_label((keys >> 8) & 0xff, special_key);
    std::snprintf(refill_text, sizeof(refill_text), "%s%s", refill_key,
                  !known ? " ?" : pending ? " ..." : !connected ? " OFF" : "");
    std::snprintf(special_text, sizeof(special_text), "%s", special_key);
    const Point offset{-step.y * 0.35f, step.x * 0.35f};
    if (!label(refill_label, refill_text, {refill_at.x + offset.x, refill_at.y + offset.y})) {
        refuse("hud_refill_text_absent", element, refill_label, epoch); return;
    }
    if (switch_available &&
        !label(special_label, special_text, {anchor.x + offset.x * 2, anchor.y + offset.y * 2})) {
        refuse("hud_special_text_absent", element, special_label, epoch); return;
    }
    swf.position(refill, refill_at.x, refill_at.y);
    swf.visible(refill, true, true);
    swf.visible(refill_label, true, true);
    swf.visible(special_label, switch_available, true);
    if (switch_available) {
        swf.position(special_arrow, anchor.x + offset.x, anchor.y + offset.y);
        swf.visible(special_arrow, true, true);
    }
    hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded,
        "hud_projection_applied", 0,
        {{"owner", element}, {"parent", parent}, {"generation", epoch}, {"created", created},
         {"snapshot_valid", owner.namespace_valid}, {"policy", owner.selected}, {"balance", owner.refill_balance},
         {"pending", pending}, {"connected", connected}, {"binds", keys},
         {"snapshot_revision", owner.revision}, {"request_revision", owner.request_revision},
         {"render_callback_observed", 0}, {"pixels_observed", 0},
         {"switch_source", arrow_source}, {"switch_clip", special_arrow}});
}
} // namespace hud
