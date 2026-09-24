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
int64_t trace_point(Point value) {
    static_assert(sizeof(Point) == sizeof(uint64_t));
    uint64_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<int64_t>(bits);
}
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

struct Rect { Point tl, br; };
bool raw_bounds(uintptr_t clip, Rect& out) {
    if (!clip) return false;
    out = {*reinterpret_cast<Point*>(clip + 0xa8), *reinterpret_cast<Point*>(clip + 0xb0)};
    return std::isfinite(out.tl.x) && std::isfinite(out.tl.y) &&
        std::isfinite(out.br.x) && std::isfinite(out.br.y) &&
        out.br.x > out.tl.x && out.br.y > out.tl.y;
}
Point center(Rect rect) { return {(rect.tl.x + rect.br.x) * 0.5f,
                                  (rect.tl.y + rect.br.y) * 0.5f}; }

bool unrender(uintptr_t clip, Point point, Point& out) {
    // RenderSprite (0x18341d0) stores the rendered bounds at +0xa8 and
    // their local-to-render matrix at +0x90. Both describe the same render.
    const auto m = reinterpret_cast<const float*>(clip + 0x90);
    const float det = m[0] * m[1] - m[2] * m[3];
    const float area = std::fabs(m[0] * m[1]) + std::fabs(m[2] * m[3]);
    if (!std::isfinite(det) || std::fabs(det) <= area * 0.000001f) return false;
    point = {point.x - m[4], point.y - m[5]};
    out = {(m[1] * point.x - m[2] * point.y) / det,
           (m[0] * point.y - m[3] * point.x) / det};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

uintptr_t transform_slot(uintptr_t clip) {
    if (!clip) return 0;
    const auto slot = *reinterpret_cast<int32_t*>(clip + 0xc);
    const auto context = *reinterpret_cast<uintptr_t*>(clip + 0x10);
    if (slot < 0 || !context) return 0;
    const auto transforms = *reinterpret_cast<uintptr_t*>(context + 0x80);
    return transforms ? transforms + static_cast<uintptr_t>(slot) * 0x40 : 0;
}

bool affine_to(uintptr_t clip, uintptr_t ancestor, uintptr_t movie, Point point, Point& out) {
    for (unsigned i = 0; i < 16 && clip != ancestor; ++i) {
        if (!clip || *reinterpret_cast<uintptr_t*>(clip + 0x30) != movie) return false;
        const auto t = transform_slot(clip);
        if (!t) return false;
        point = {*reinterpret_cast<float*>(t + 0x4) * point.x +
                     *reinterpret_cast<float*>(t + 0xc) * point.y +
                     *reinterpret_cast<float*>(t + 0x14),
                 *reinterpret_cast<float*>(t + 0x10) * point.x +
                     *reinterpret_cast<float*>(t + 0x8) * point.y +
                     *reinterpret_cast<float*>(t + 0x18)};
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
        clip = *reinterpret_cast<uintptr_t*>(clip + 0x40);
    }
    out = point;
    return clip == ancestor;
}

bool bounds(uintptr_t clip, uintptr_t parent, Rect& out) {
    // Convert the cached render rectangle directly into WeaponInfo space.
    Rect local{};
    if (!raw_bounds(clip, local)) return false;
    const auto movie = *reinterpret_cast<uintptr_t*>(clip + 0x30);
    const Point corners[4]{{local.tl.x, local.tl.y}, {local.br.x, local.tl.y},
                           {local.tl.x, local.br.y}, {local.br.x, local.br.y}};
    Point point{}, local_point{};
    if (!unrender(clip, corners[0], local_point) ||
        !affine_to(clip, parent, movie, local_point, point)) return false;
    out = {point, point};
    for (unsigned i = 1; i < 4; ++i) {
        if (!unrender(clip, corners[i], local_point) ||
            !affine_to(clip, parent, movie, local_point, point)) return false;
        out.tl.x = std::min(out.tl.x, point.x);
        out.tl.y = std::min(out.tl.y, point.y);
        out.br.x = std::max(out.br.x, point.x);
        out.br.y = std::max(out.br.y, point.y);
    }
    return out.br.x > out.tl.x && out.br.y > out.tl.y;
}

bool from_space(uintptr_t clip, uintptr_t parent, uintptr_t movie,
                Point point, Point& out, bool delta = false) {
    Point origin{}, x{}, y{};
    if (!affine_to(clip, parent, movie, {0, 0}, origin) ||
        !affine_to(clip, parent, movie, {1, 0}, x) ||
        !affine_to(clip, parent, movie, {0, 1}, y)) return false;
    x = {x.x - origin.x, x.y - origin.y};
    y = {y.x - origin.x, y.y - origin.y};
    const float det = x.x * y.y - y.x * x.y;
    const float area = std::fabs(x.x * y.y) + std::fabs(y.x * x.y);
    if (!std::isfinite(det) || std::fabs(det) <= area * 0.000001f) return false;
    if (!delta) point = {point.x - origin.x, point.y - origin.y};
    out = {(y.y * point.x - y.x * point.y) / det,
           (x.x * point.y - x.y * point.x) / det};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

// Match the donor's complete visual transform when a nested sprite is cloned
// under WeaponInfo. Position is written separately by swf.position.
bool match_linear(uintptr_t clone, uintptr_t source, uintptr_t parent, uintptr_t movie,
                  float scale = 1.0f) {
    Point origin{}, x{}, y{};
    if (!affine_to(source, parent, movie, {0, 0}, origin) ||
        !affine_to(source, parent, movie, {1, 0}, x) ||
        !affine_to(source, parent, movie, {0, 1}, y)) return false;
    const auto t = transform_slot(clone);
    if (!t) return false;
    const float values[4]{(x.x - origin.x) * scale, (y.y - origin.y) * scale,
                            (y.x - origin.x) * scale, (x.y - origin.y) * scale};
    const unsigned offsets[4]{0x4, 0x8, 0xc, 0x10};
    bool changed = false;
    for (unsigned i = 0; i < 4; ++i) {
        auto& current = *reinterpret_cast<float*>(t + offsets[i]);
        if (current != values[i]) { current = values[i]; changed = true; }
    }
    if (changed) swf.dirty(clone);
    return true;
}

bool visual_offset(uintptr_t source, uintptr_t visual_leaf, uintptr_t parent,
                   uintptr_t movie, Point& out) {
    Rect rect{};
    Point origin{};
    if (!bounds(visual_leaf, parent, rect) ||
        !affine_to(source, parent, movie, {0, 0}, origin)) return false;
    const Point visual = center(rect);
    out = {visual.x - origin.x, visual.y - origin.y};
    return true;
}

bool place_visual(uintptr_t clone, uintptr_t source, uintptr_t parent,
                  uintptr_t movie, Point desired, Point offset, float scale = 1.0f) {
    if (!match_linear(clone, source, parent, movie, scale)) return false;
    place(clone, {desired.x - offset.x * scale, desired.y - offset.y * scale});
    return true;
}

struct NativePosition {
    uintptr_t root = 0, parent = 0, movie = 0;
    uint64_t epoch = 0;
    Point base{}, applied{}, local_center{};
};

bool remember_native(NativePosition& saved, uintptr_t root, uintptr_t movie, uint64_t epoch) {
    Point current{};
    if (!root || *reinterpret_cast<uintptr_t*>(root + 0x30) != movie ||
        !position(root, current)) return false;
    const auto parent = *reinterpret_cast<uintptr_t*>(root + 0x40);
    Rect rect{};
    Point local{};
    if (!raw_bounds(root, rect) || !unrender(root, center(rect), local)) return false;
    if (saved.root != root || saved.parent != parent || saved.movie != movie ||
        saved.epoch != epoch || current.x != saved.applied.x || current.y != saved.applied.y) {
        saved = {root, parent, movie, epoch, current, current, local};
    }
    saved.local_center = local;
    return true;
}

bool native_center(const NativePosition& saved, uintptr_t parent, Point& out) {
    const auto t = transform_slot(saved.root);
    if (!t) return false;
    const Point local{
        *reinterpret_cast<float*>(t + 0x4) * saved.local_center.x +
            *reinterpret_cast<float*>(t + 0xc) * saved.local_center.y + saved.base.x,
        *reinterpret_cast<float*>(t + 0x10) * saved.local_center.x +
            *reinterpret_cast<float*>(t + 0x8) * saved.local_center.y + saved.base.y};
    return affine_to(saved.parent, parent, saved.movie, local, out);
}

bool move_native(NativePosition& saved, uintptr_t parent, Point parent_delta) {
    Point local{};
    if (!from_space(saved.parent, parent, saved.movie, parent_delta, local, true)) return false;
    saved.applied = {saved.base.x + local.x, saved.base.y + local.y};
    place(saved.root, saved.applied);
    return true;
}

void restore_native(NativePosition& saved, uintptr_t root, uintptr_t movie, uint64_t epoch) {
    if (root && saved.root == root && saved.movie == movie && saved.epoch == epoch) {
        place(root, saved.base);
        saved.applied = saved.base;
    }
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
    uintptr_t widget = 0, hammer = 0, quickuse = 0, flame = 0;
    uintptr_t primary_root = 0, flame_root = 0, crucible_root = 0, hammer_root = 0;
    uintptr_t parent = 0, movie = 0, source = 0;
    bool crucible_movie_match = false, hammer_movie_match = false;
};

GraphicsSource graphics_source(uintptr_t element) {
    GraphicsSource s{};
    s.widget = *reinterpret_cast<uintptr_t*>(element + 0x1e8);
    s.hammer = *reinterpret_cast<uintptr_t*>(element + 0x1f8);
    s.quickuse = *reinterpret_cast<uintptr_t*>(element + 0x1d0);
    s.flame = *reinterpret_cast<uintptr_t*>(element + 0x1d8);
    s.primary_root = s.quickuse ? *reinterpret_cast<uintptr_t*>(s.quickuse + 0x18) : 0;
    s.flame_root = s.flame ? *reinterpret_cast<uintptr_t*>(s.flame + 0x18) : 0;
    s.crucible_root = s.widget ? *reinterpret_cast<uintptr_t*>(s.widget + 0x18) : 0;
    s.hammer_root = s.hammer ? *reinterpret_cast<uintptr_t*>(s.hammer + 0x18) : 0;
    s.parent = s.primary_root ? *reinterpret_cast<uintptr_t*>(s.primary_root + 0x40) : 0;
    s.movie = s.parent ? *reinterpret_cast<uintptr_t*>(s.parent + 0x30) : 0;
    if (!s.movie || !s.flame_root ||
        *reinterpret_cast<uintptr_t*>(s.primary_root + 0x30) != s.movie ||
        *reinterpret_cast<uintptr_t*>(s.flame_root + 0x30) != s.movie ||
        *reinterpret_cast<uintptr_t*>(s.flame_root + 0x40) != s.parent) return s;
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
    static NativePosition crucible_position{}, hammer_position{};
    if (!graphics_ready) {
        restore_native(crucible_position, context.crucible_root, context.movie, epoch);
        restore_native(hammer_position, context.hammer_root, context.movie, epoch);
        refuse("hud_graphics_unavailable", element, 0, epoch);
        return false;
    }
    static std::mutex state_mutex;
    const auto source = context.source;
    // The refill template supplies pips; geometry comes from a rendered Special.
    auto layout_source = source;
    const auto parent = context.parent, movie = context.movie;
    Rect source_probe{};
    if (source && !bounds(source, parent, source_probe)) {
        const auto selected_root = owner.selected == SC_SPECIAL_WEAPON_HAMMER ?
            context.hammer_root : context.crucible_root;
        if (selected_root && bounds(selected_root, parent, source_probe) &&
            child(selected_root, "icon")) layout_source = selected_root;
    }
    const auto primary_root = context.primary_root, flame_root = context.flame_root;
    const auto crucible_root = context.crucible_root, quickuse = context.quickuse;
    const auto widget = context.widget, hammer = context.hammer;
    struct Refusal { uintptr_t element, widget, hammer, primary, flame, parent, movie; uint64_t epoch; };
    static Refusal last{};
    if (!source) {
        restore_native(crucible_position, context.crucible_root, movie, epoch);
        restore_native(hammer_position, context.hammer_root, movie, epoch);
        if (parent && movie && primary_root && flame_root &&
            *reinterpret_cast<uintptr_t*>(parent + 0x30) == movie &&
            *reinterpret_cast<uintptr_t*>(primary_root + 0x30) == movie &&
            *reinterpret_cast<uintptr_t*>(primary_root + 0x40) == parent &&
            *reinterpret_cast<uintptr_t*>(flame_root + 0x30) == movie &&
            *reinterpret_cast<uintptr_t*>(flame_root + 0x40) == parent) {
            for (const auto name : {"apAmmoRefill", "apAmmoGlyph", "apSpecialSwitch",
                                     "apAmmoRefillBind", "apSpecialToggleBind"})
                if (const auto clip = child(parent, name)) show(clip, false);
        }
        const Refusal current{element, widget, hammer, primary_root, flame_root, parent, movie, epoch};
        {
            std::lock_guard lock(state_mutex);
            if (last.element == current.element && last.widget == current.widget &&
                last.hammer == current.hammer && last.primary == current.primary &&
                last.flame == current.flame && last.parent == current.parent &&
                last.movie == current.movie && last.epoch == current.epoch) return false;
            last = current;
        }
        hud_trace.record(save::BStage::profile_output, save::BStatus::refused,
            "hud_graphics_source_unavailable", 0,
            {{"owner", element}, {"widget", widget}, {"hammer", hammer},
              {"quickuse", quickuse}, {"flame", context.flame},
              {"primary_root", primary_root}, {"flame_root", flame_root},
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
        uintptr_t element = 0, parent = 0, movie = 0, source = 0, primary = 0, flame = 0;
        uint64_t epoch = 0, revision = 0, request_revision = 0;
        uint32_t balance = UINT32_MAX, flags = 0, request_state = 0, selected = 0;
        uint32_t owns_crucible = 0, owns_hammer = 0;
        unsigned keys = 0;
        bool namespace_valid = false, keycaps = false, element_visible = false, presented = false;
        bool graphics_applied = false, fully_applied = false;
        uint64_t retry_at = 0;
    };
    static RenderState shared_rendered{};
    const auto save_render = [&](RenderState state) {
        std::lock_guard lock(state_mutex);
        shared_rendered = state;
    };
    const RenderState current{element, parent, movie, source, primary_root, flame_root,
        epoch, owner.revision, owner.request_revision, owner.refill_balance, owner.refill_flags,
        owner.refill_request_state, owner.selected, owner.owns_crucible, owner.owns_hammer,
        keys, owner.namespace_valid, keycap_ready,
        *reinterpret_cast<uint8_t*>(element + 0x209) != 0};
    Point anchor{}, adjacent{};
    if (!position(primary_root, anchor) || !position(flame_root, adjacent)) {
        refuse("hud_swf_context_unavailable", element, source, epoch); return false;
    }
    auto refill = child(parent, "apAmmoRefill");
    auto ammo_glyph = child(parent, "apAmmoGlyph");
    auto special_arrow = child(parent, "apSpecialSwitch");
    auto refill_bind = child(parent, "apAmmoRefillBind");
    auto special_bind = child(parent, "apSpecialToggleBind");
    const bool visible = owner.namespace_valid && *reinterpret_cast<uint8_t*>(element + 0x209);
    // All references come from this update's live parent, including invalidation.
    if (!visible) {
        restore_native(crucible_position, context.crucible_root, movie, epoch);
        restore_native(hammer_position, context.hammer_root, movie, epoch);
        if (refill) show(refill, false);
        if (ammo_glyph) show(ammo_glyph, false);
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
    bool created = false;
    const bool switch_available = owner.owns_crucible && owner.owns_hammer &&
        route_ready.load(std::memory_order_acquire) && ((keys >> 8) & 0xff);
    const auto arrow_source = child(parent, "swapEquipment");
    const auto native_arrow = child(arrow_source, "arrow");
    const auto active_root = owner.selected == SC_SPECIAL_WEAPON_CRUCIBLE ? context.crucible_root :
        owner.selected == SC_SPECIAL_WEAPON_HAMMER ? context.hammer_root : uintptr_t{0};
    auto& active_position = owner.selected == SC_SPECIAL_WEAPON_HAMMER ? hammer_position : crucible_position;
    auto& inactive_position = owner.selected == SC_SPECIAL_WEAPON_HAMMER ? crucible_position : hammer_position;
    const auto inactive_root = owner.selected == SC_SPECIAL_WEAPON_HAMMER ? context.crucible_root : context.hammer_root;
    Rect equipment_bounds{}, arrow_bounds{}, source_bounds{}, flame_bounds{}, active_bounds{};
    const bool equipment_visual = bounds(primary_root, parent, equipment_bounds);
    const bool native_arrow_visual = bounds(native_arrow, parent, arrow_bounds);
    const bool source_visual = bounds(layout_source, parent, source_bounds);
    const bool flame_visual = bounds(context.flame_root, parent, flame_bounds);
    const bool active_visual = bounds(active_root, parent, active_bounds);
    const bool active_movie = active_root &&
        *reinterpret_cast<uintptr_t*>(active_root + 0x30) == movie;
    Point active_now{};
    const bool active_position_valid = active_movie && position(active_root, active_now);
    const bool active_cached = active_visual && active_position_valid &&
        remember_native(active_position, active_root, movie, epoch);
    Point original_special{};
    const bool active_stage_forward = active_cached &&
        native_center(active_position, parent, original_special);
    auto& source_position = layout_source == context.hammer_root ? hammer_position : crucible_position;
    Point original_source{};
    const bool source_cached = source_visual &&
        remember_native(source_position, layout_source, movie, epoch) &&
        native_center(source_position, parent, original_source);
    // Full widget bounds include CTA art; placement uses at most one native slot.
    const float slot_width = std::min(flame_bounds.br.x - flame_bounds.tl.x,
                                      std::fabs(anchor.x - adjacent.x));
    const float flame_left = center(flame_bounds).x - slot_width * 0.5f;
    float native_gap = flame_bounds.tl.x - original_source.x -
        (source_bounds.br.x - source_bounds.tl.x) * 0.5f;
    if (native_gap <= 0) {
        const auto source_icon = child(layout_source, "icon");
        const auto flame_icon = child(context.flame_root, "icon");
        Rect source_icon_bounds{}, flame_icon_bounds{};
        Point source_icon_offset{};
        if (bounds(source_icon, parent, source_icon_bounds) &&
            bounds(flame_icon, parent, flame_icon_bounds) &&
            visual_offset(layout_source, source_icon, parent, movie, source_icon_offset))
            native_gap = flame_icon_bounds.tl.x - original_source.x - source_icon_offset.x -
                (source_icon_bounds.br.x - source_icon_bounds.tl.x) * 0.5f;
    }
    // Slot bounds include CTA art; spacing is cosmetic and cannot block HUD projection.
    native_gap = std::max(1.0f, native_gap);
    const bool native_layout = native_arrow_visual && flame_visual &&
        active_stage_forward && source_cached && std::isfinite(native_gap) && native_gap > 0;
    const Point equipment_center = center(equipment_bounds);
    const Point arrow_center = center(arrow_bounds);
    const Point refill_target{arrow_bounds.br.x + native_gap +
        slot_width * 0.5f, center(flame_bounds).y};
    Point refill_offset{}, arrow_offset{}, toggle_target{};
    const bool refill_visual = equipment_visual && native_arrow_visual &&
        flame_visual && active_visual && source_cached &&
        std::isfinite(native_gap) && native_gap > 0 &&
        visual_offset(layout_source, layout_source, parent, movie, refill_offset);
    const bool arrow_visual = native_layout &&
        visual_offset(arrow_source, native_arrow, parent, movie, arrow_offset);
    const char* switch_failure = nullptr;
    if (switch_available) {
        if (!arrow_source) switch_failure = "switch_source_missing";
        else if (!native_arrow) switch_failure = "switch_arrow_leaf_missing";
        else if (!native_arrow_visual) {
            Rect raw{};
            switch_failure = raw_bounds(native_arrow, raw) ?
                "switch_arrow_stage_transform_failed" : "switch_arrow_bounds_missing";
        }
        else if (!active_root) switch_failure = "switch_active_special_root_missing";
        else if (!active_visual) {
            Rect raw{};
            switch_failure = raw_bounds(active_root, raw) ?
                "switch_active_stage_transform_failed" : "switch_active_special_bounds_missing";
        }
        else if (!active_movie) switch_failure = "switch_active_special_movie_mismatch";
        else if (!active_position_valid) switch_failure = "switch_active_special_position_missing";
        else if (!active_cached) switch_failure = "switch_active_bounds_cache_failed";
        else if (!active_stage_forward) switch_failure = "switch_active_stage_forward_failed";
        else if (!source_visual) switch_failure = "switch_source_bounds_missing";
        else if (!source_cached) switch_failure = "switch_source_bounds_cache_failed";
        else if (!flame_visual) {
            Rect raw{};
            switch_failure = raw_bounds(flame_root, raw) ?
                "switch_flame_stage_transform_failed" : "switch_flame_bounds_missing";
        }
        else if (!native_layout) switch_failure = "switch_native_gap_invalid";
        else if (!equipment_visual) switch_failure = "switch_equipment_bounds_missing";
        else if (!arrow_visual) switch_failure = "switch_arrow_stage_forward_failed";
    }
    if (!switch_failure && switch_available) {
        const float arrow_width = arrow_bounds.br.x - arrow_bounds.tl.x;
        const float arrow_right = flame_left - native_gap;
        const float arrow_left = arrow_right - arrow_width;
        const float desired_special_right = arrow_left - native_gap;
        toggle_target = {arrow_left + arrow_width * 0.5f, arrow_center.y};
        if (!(desired_special_right < arrow_left && arrow_right < flame_left))
            switch_failure = "switch_layout_overlap";
    }
    bool arrow_ready = false;
    if (switch_available && !switch_failure) {
        if (!special_arrow) special_arrow = clone(arrow_source, parent, "apSpecialSwitch", created);
        if (!special_arrow) switch_failure = "switch_clone_failed";
        else {
            hold_frame(special_arrow, 1);
            if (const auto backer = child(special_arrow, "backer")) show(backer, false);
            if (const auto cta = child(special_arrow, "cta")) show(cta, false);
            if (const auto cta = child(child(special_arrow, "icon"), "cta")) show(cta, false);
            if (const auto leaf = child(special_arrow, "arrow")) show(leaf, true);
            else switch_failure = "switch_arrow_leaf_missing";
            if (!switch_failure &&
                !place_visual(special_arrow, arrow_source, parent, movie, toggle_target, arrow_offset))
                switch_failure = "switch_arrow_place_failed";
            const float desired_special_right = toggle_target.x -
                (arrow_bounds.br.x - arrow_bounds.tl.x) * 0.5f - native_gap;
            const Point special_target{desired_special_right - slot_width * 0.5f,
                                       center(flame_bounds).y};
            const Point shift{special_target.x - original_special.x,
                              special_target.y - original_special.y};
            if (!switch_failure && !move_native(active_position, parent, shift))
                switch_failure = "switch_move_active_failed";
            if (!switch_failure) {
                Rect inactive_bounds{};
                Point inactive_center{};
                if (bounds(inactive_root, parent, inactive_bounds) &&
                    remember_native(inactive_position, inactive_root, movie, epoch) &&
                    native_center(inactive_position, parent, inactive_center)) {
                    if (!move_native(inactive_position, parent,
                            {special_target.x - inactive_center.x, special_target.y - inactive_center.y}))
                        restore_native(inactive_position, inactive_root, movie, epoch);
                } else restore_native(inactive_position, inactive_root, movie, epoch);
            }
            if (!switch_failure) { show(special_arrow, true); arrow_ready = true; }
        }
    }
    if (!arrow_ready) {
        restore_native(crucible_position, context.crucible_root, movie, epoch);
        restore_native(hammer_position, context.hammer_root, movie, epoch);
        if (special_arrow) show(special_arrow, false);
    }
    const float special_half_width = (active_bounds.br.x - active_bounds.tl.x) * 0.5f;
    const float special_half_height = (active_bounds.br.y - active_bounds.tl.y) * 0.5f;
    const Rect native_special_bounds{{original_special.x - special_half_width,
                                      original_special.y - special_half_height},
                                     {original_special.x + special_half_width,
                                      original_special.y + special_half_height}};
    const float arrow_half_width = (arrow_bounds.br.x - arrow_bounds.tl.x) * 0.5f;
    const float arrow_half_height = (arrow_bounds.br.y - arrow_bounds.tl.y) * 0.5f;
    const Rect ap_arrow_bounds{{toggle_target.x - arrow_half_width,
                                toggle_target.y - arrow_half_height},
                               {toggle_target.x + arrow_half_width,
                                toggle_target.y + arrow_half_height}};
    const float refill_half_width = (source_bounds.br.x - source_bounds.tl.x) * 0.5f;
    const float refill_half_height = (source_bounds.br.y - source_bounds.tl.y) * 0.5f;
    const Rect intended_refill_bounds{{refill_target.x - refill_half_width,
                                       refill_target.y - refill_half_height},
                                      {refill_target.x + refill_half_width,
                                       refill_target.y + refill_half_height}};
    hud_trace.record(save::BStage::profile_prepare,
        switch_failure || !refill_visual ? save::BStatus::refused : save::BStatus::succeeded,
        switch_failure ? switch_failure :
            refill_visual ? "hud_visual_layout_ready" : "hud_visual_layout_unavailable",
        epoch, {{"parent", parent}, {"movie", movie}, {"selected", owner.selected},
                {"active_root", active_root}, {"flame_root", context.flame_root},
                {"native_arrow", native_arrow},
                {"special_tl", trace_point(native_special_bounds.tl)},
                {"special_br", trace_point(native_special_bounds.br)},
                {"flame_tl", trace_point(flame_bounds.tl)},
                {"flame_br", trace_point(flame_bounds.br)},
                {"native_arrow_tl", trace_point(arrow_bounds.tl)},
                {"native_arrow_br", trace_point(arrow_bounds.br)},
                {"ap_arrow_tl", trace_point(ap_arrow_bounds.tl)},
                {"ap_arrow_br", trace_point(ap_arrow_bounds.br)},
                {"refill_tl", trace_point(intended_refill_bounds.tl)},
                {"refill_br", trace_point(intended_refill_bounds.br)}}, element);
    if (!refill_visual) {
        if (refill) show(refill, false);
        if (ammo_glyph) show(ammo_glyph, false);
        if (refill_bind) show(refill_bind, false);
        if (special_bind) show(special_bind, false);
        refuse(switch_failure ? switch_failure :
            !source_visual ? "hud_refill_source_bounds_missing" :
            !native_arrow_visual ? "hud_refill_arrow_bounds_missing" :
            !flame_visual ? "hud_refill_flame_bounds_missing" :
            !equipment_visual ? "hud_refill_equipment_bounds_missing" :
            !active_visual ? "hud_refill_special_bounds_missing" :
            !std::isfinite(native_gap) || native_gap <= 0 ? "hud_refill_native_gap_invalid" :
            "hud_refill_stage_transform_failed", element, source, epoch);
        return false;
    }
    const bool new_refill = !refill;
    if (!refill) refill = clone(source, parent, "apAmmoRefill", created);
    if (!refill) { refuse("hud_refill_clone_failed", element, source, epoch); return false; }
    const auto fail_refill = [&](const char* reason, uintptr_t detail) {
        show(refill, false);
        if (ammo_glyph) show(ammo_glyph, false);
        if (refill_bind) show(refill_bind, false);
        refuse(reason, element, detail, epoch);
        return false;
    };
    const bool known = owner.refill_balance <= 3;
    hold_frame(refill, owner.refill_enabled ? 1 : 2);
    // The equipment clone carries its donor CTA; the AP labels are separate clips.
    if (const auto cta = child(child(refill, "icon"), "cta")) show(cta, false);
    const auto icon_group = child(refill, "icon");
    if (const auto small = child(icon_group, "iconSmall")) show(small, false);
    if (const auto static_icon = child(icon_group, "iconStatic")) show(static_icon, false);
    // WeaponInfo::Setup binds +0x248 to equippedWeapon; +0x10 is its resolved sprite.
    const auto equipped_weapon = *reinterpret_cast<uintptr_t*>(element + 0x258);
    const auto native_ammo = child(child(equipped_weapon, "ammoIcon"), "image");
    if (!native_ammo) return fail_refill("hud_ammo_leaf_missing", equipped_weapon);
    if (*reinterpret_cast<uintptr_t*>(native_ammo + 0x30) != movie)
        return fail_refill("hud_ammo_leaf_movie_mismatch", native_ammo);
    const auto ammo_icon_path_hash = *reinterpret_cast<int32_t*>(native_ammo + 0x20);
    Rect native_ammo_bounds{}, icon_bounds{};
    if (!bounds(native_ammo, parent, native_ammo_bounds) ||
        !bounds(child(layout_source, "icon"), parent, icon_bounds))
        return fail_refill("hud_ammo_leaf_bounds_missing", native_ammo);
    if (!ammo_glyph) ammo_glyph = clone(native_ammo, parent, "apAmmoGlyph", created);
    if (!ammo_glyph) return fail_refill("hud_ammo_leaf_clone_failed", native_ammo);
    const float glyph_width = native_ammo_bounds.br.x - native_ammo_bounds.tl.x;
    const float glyph_height = native_ammo_bounds.br.y - native_ammo_bounds.tl.y;
    const float icon_width = icon_bounds.br.x - icon_bounds.tl.x;
    const float icon_height = icon_bounds.br.y - icon_bounds.tl.y;
    const float width_scale = icon_width / glyph_width;
    const float height_scale = icon_height / glyph_height;
    const float glyph_scale = width_scale < height_scale ? width_scale : height_scale;
    if (!std::isfinite(glyph_scale) || glyph_scale <= 0)
        return fail_refill("hud_ammo_leaf_scale_invalid", native_ammo);
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
        attempt.icon != ammo_glyph || attempt.epoch != epoch)
        attempt = {element, parent, refill, ammo_glyph, 0, epoch, 0, false};
    if (attempt.applied && *reinterpret_cast<uintptr_t*>(ammo_glyph + 0x60) != attempt.material) {
        attempt.applied = false;
        attempt.retry_at = 0;
    }
    if (!attempt.applied && GetTickCount64() >= attempt.retry_at) {
        attempt.retry_at = GetTickCount64() + 1000;
        const auto material = swf.find_material(image_base + 0x5e05200, "art/ui/icons/ammo/bullets", 0);
        if (material) {
            swf.material(ammo_glyph, material, -1, -1, 0);
            attempt.material = material;
            attempt.applied = *reinterpret_cast<uintptr_t*>(ammo_glyph + 0x60) == material &&
                *reinterpret_cast<uint16_t*>(ammo_glyph + 0x68) != 0 &&
                *reinterpret_cast<uint16_t*>(ammo_glyph + 0x6a) != 0;
        }
    }
    {
        std::lock_guard lock(state_mutex);
        shared_attempt = attempt;
    }
    if (!attempt.applied) {
        return fail_refill("hud_ammo_material_unavailable", ammo_glyph);
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
    if (!place_visual(refill, layout_source, parent, movie, refill_target, refill_offset))
        return fail_refill("hud_refill_place_failed", refill);
    Point glyph_offset{};
    if (!visual_offset(native_ammo, native_ammo, parent, movie, glyph_offset))
        return fail_refill("hud_ammo_leaf_transform_failed", native_ammo);
    Point icon_offset{};
    if (!visual_offset(layout_source, child(layout_source, "icon"), parent, movie, icon_offset))
        return fail_refill("hud_ammo_icon_bounds_missing", layout_source);
    const Point glyph_target{refill_target.x + icon_offset.x - refill_offset.x,
                             refill_target.y + icon_offset.y - refill_offset.y};
    if (!place_visual(ammo_glyph, native_ammo, parent, movie,
                      glyph_target, glyph_offset, glyph_scale))
        return fail_refill("hud_ammo_leaf_place_failed", ammo_glyph);
    Rect glyph_bounds{};
    bounds(ammo_glyph, parent, glyph_bounds);
    show(ammo_glyph, true);
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
    if (!donor_root || *reinterpret_cast<uintptr_t*>(donor_root + 0x40) != parent ||
        !donor ||
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
    Rect donor_bounds{};
    const bool donor_visual = bounds(child(donor, "kbm"), parent, donor_bounds);
    if (!donor_visual) {
        if (refill_bind) show(refill_bind, false);
        if (special_bind) show(special_bind, false);
        refuse("hud_keycap_donor_bounds_missing", element, donor, epoch);
        return graphics_applied;
    }
    const float keycap_y = center(donor_bounds).y - equipment_center.y;
    const Point refill_key_target{refill_target.x, arrow_center.y + keycap_y};
    const Point toggle_key_target{toggle_target.x, toggle_target.y + keycap_y};
    const char* bind_failure = nullptr;
    const auto bind_key = [&](uintptr_t& clip, const char* name, unsigned vk,
                               Point visual_at,
                               const char* missing_clip, const char* missing_text) {
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
        Point visible_offset{};
        if (!match_linear(clip, donor, parent, movie) ||
            (!visual_offset(clip, kbm, parent, movie, visible_offset) &&
             !visual_offset(donor, child(donor, "kbm"), parent, movie, visible_offset)) ||
            !place_visual(clip, donor, parent, movie, visual_at, visible_offset)) {
            show(clip, false);
            bind_failure = "hud_keycap_visual_bounds_missing";
            return false;
        }
        show(clip, true);
        return true;
    };
    const auto ammo_key = keys & 0xff;
    const auto toggle_key = arrow_ready ? (keys >> 8) & 0xff : 0;
    if (!bind_key(refill_bind, "apAmmoRefillBind", ammo_key,
                   refill_key_target,
                  "ammo_keycap_missing", "ammo_text_failed") ||
        !bind_key(special_bind, "apSpecialToggleBind", toggle_key,
                   toggle_key_target,
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
    Rect f9_bounds{}, f10_bounds{};
    bounds(child(refill_bind, "kbm"), parent, f9_bounds);
    if (arrow_ready) bounds(child(special_bind, "kbm"), parent, f10_bounds);
    hud_trace.record(save::BStage::profile_output, save::BStatus::succeeded,
        "hud_native_keycaps_applied", 0,
        {{"owner", element}, {"parent", parent}, {"generation", epoch},
         {"selected", owner.selected}, {"balance", owner.refill_balance},
         {"binds", keys}, {"pip_frame_after", *reinterpret_cast<uint16_t*>(three + 0x58)},
         {"ammo_source", native_ammo}, {"ammo_glyph", ammo_glyph},
         {"ammo_full_path_hash", ammo_icon_path_hash}, {"ammo_material", attempt.material},
         {"glyph_tl", trace_point(glyph_bounds.tl)},
         {"glyph_br", trace_point(glyph_bounds.br)},
         {"f9_center", trace_point(center(f9_bounds))},
         {"f10_center", trace_point(center(f10_bounds))},
         {"pixels_observed", 0}});
    return graphics_applied;
}
} // namespace hud
