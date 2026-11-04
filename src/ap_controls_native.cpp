#include "engine/builds/build_descriptor.h"
#include "ap_controls_native.h"
#include "player_strings_generated.h"
#include "local_controls.h"
#include "native_target.h"
#include "save_session.h"
#include <MinHook.h>
#include "engine/hooks/hook_set.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace sentinel::ap_controls {
namespace {
hooks::HookSet hook_set("ap_controls");
using Update = void(*)(uintptr_t);
using Show = void(*)(uintptr_t, int);
using Destroy = uintptr_t(*)(uintptr_t, uint32_t);
using Action = bool(*)(uintptr_t, const void*);
using Allocate = uintptr_t(*)(size_t);
using Construct = uintptr_t(*)(uintptr_t);
using Configure = void(*)(uintptr_t, const char*, const char*, const char*, uint32_t, uint8_t);
using Group = void(*)(uintptr_t, int, const char*);
using Capture = void(*)(uintptr_t, uint8_t);
using Assign = void(*)(uintptr_t, const char*);
using WidgetText = uintptr_t(*)(uintptr_t);
using Bound = const char*(*)(uint32_t, uint32_t, int, uint8_t*);
using MenuActive = bool(*)(uintptr_t);
struct Value { uint32_t type = 0, reserved = 0; uintptr_t payload = 0; };
using Lookup = Value*(*)(uintptr_t, Value*, const char*, const char*, const char*,
                        const char*, const char*, const char*, const char*);
using Text = uintptr_t(*)(const Value*);
using Release = void(*)(Value*);
Update original_build = nullptr, original_finalize = nullptr, original_render = nullptr;
Update original_hide = nullptr, original_chat_begin = nullptr, original_chat_end = nullptr;
Show original_show = nullptr;
Destroy original_destroy = nullptr;
Destroy original_chat_destroy = nullptr;
Action original_action = nullptr, original_key = nullptr;
Allocate allocate = nullptr;
Construct construct = nullptr;
Configure configure = nullptr;
Group add_group = nullptr;
Capture begin_capture = nullptr;
Update end_capture = nullptr;
Assign assign_string = nullptr, assign_text = nullptr;
WidgetText widget_text = nullptr;
Bound bound_command = nullptr;
MenuActive menu_active = nullptr;
Lookup lookup = nullptr;
Text text_instance = nullptr;
Release release_value = nullptr;
uintptr_t input_object = 0;
uintptr_t console_object = 0;
uintptr_t menu_pointer = 0;
std::atomic<bool> enabled{false};
std::atomic<uintptr_t> visible_popup{0};
std::atomic<uintptr_t> editing_chat{0};
thread_local uintptr_t building_popup = 0;
wchar_t queue_directory[520]{};
controls::Bindings cached{};
uint64_t next_read = 0;
unsigned errors[2]{};
constexpr const char* commands[] = {"__ap_ammo_refill", "__ap_special_toggle"};
constexpr const char* labels[] = {player_strings::controls_ammo_refill, player_strings::controls_toggle_special_weapon};
constexpr const char* messages[] = {
    "", player_strings::controls_unsupported_key, player_strings::controls_key_conflict,
    player_strings::controls_game_key_conflict, player_strings::controls_save_failed,
    player_strings::controls_restore_failed, player_strings::controls_input_not_ready, player_strings::controls_invalid_binding
};

template<class T> bool read(uintptr_t address, T& value) {
    engine::LocalMemory memory;
    return address && !memory.copy(address, &value, sizeof(value)).reason;
}
template<class F> F method(uintptr_t object, size_t offset) {
    uintptr_t table = 0, address = 0;
    if (!read(object, table) || !read(table + offset, address)) return nullptr;
    return reinterpret_cast<F>(address);
}
template<class T> void put(uintptr_t object, size_t offset, T value) {
    *reinterpret_cast<T*>(object + offset) = value;
}
int action_index(uintptr_t string) {
    int32_t length = 0;
    uintptr_t data = 0;
    char buffer[32]{};
    if (!read(string + 0x10, length) || length < 1 || length >= static_cast<int>(sizeof(buffer)) ||
        !read(string + 8, data)) return -1;
    engine::LocalMemory memory;
    if (memory.copy(data, buffer, static_cast<size_t>(length) + 1).reason || buffer[length]) return -1;
    for (int i = 0; i < 2; ++i) if (!std::strcmp(buffer, commands[i])) return i;
    return -1;
}
uintptr_t selected_row(uintptr_t popup) {
    uintptr_t list = 0, rows = 0, row = 0;
    int32_t selected = -1, count = 0;
    if (!read(popup + 0x100, list) || !read(list + 0x150, selected) ||
        !read(list + 0xa8, count) || selected < 0 || selected >= count ||
        !read(list + 0xa0, rows) || !read(rows + static_cast<size_t>(selected) * 8, row)) return 0;
    return row;
}
void refresh(bool force = false) {
    const auto now = GetTickCount64();
    if (force || now >= next_read) {
        cached = controls::read(queue_directory);
        next_read = now + 250;
    }
}
void button_label(uintptr_t button, const char* label, bool empty) {
    if (!button) return;
    assign_string(button + 0x188, label);
    put<uint8_t>(button, 0x209, 0);
    int32_t mode = 0;
    if (read(button + 0x230, mode) && mode != 2) put<int32_t>(button, 0x230, empty ? 1 : 0);
    if (const auto render = method<Update>(button, 0x20)) render(button);
}
void render_ap(uintptr_t row) {
    const auto index = action_index(row + 0x2a0);
    if (index < 0) return;
    refresh();
    char label[160]{};
    const auto error = errors[index] ? errors[index] : cached.conflict() ? 2u : (cached.invalid & (1u << index)) ? 7u : 0u;
    if (error) std::snprintf(label, sizeof(label), player_strings::controls_binding_error, labels[index], messages[error]);
    else std::snprintf(label, sizeof(label), "%s", labels[index]);
    assign_string(row + 0x188, label);
    if (const auto text = widget_text(row)) assign_text(text, label);
    uintptr_t primary = 0, secondary = 0;
    read(row + 0x338, primary);
    read(row + 0x340, secondary);
    const auto key = controls::token(cached.keys[index]);
    button_label(primary, key.c_str(), !cached.keys[index]);
    button_label(secondary, player_strings::controls_clear, false);
}
void render_detour(uintptr_t row) {
    // keep the native mouse-focus callbacks before replacing AP text.
    original_render(row);
    render_ap(row);
}
void prompt(uintptr_t popup, const char* message) {
    uintptr_t widget = 0, sprite = 0;
    if (!read(popup + 0x108, widget) || !read(widget + 0x18, sprite) || !sprite) return;
    Value value{};
    lookup(sprite, &value, "desc", "txtVal", nullptr, nullptr, nullptr, nullptr, nullptr);
    if (const auto text = text_instance(&value)) assign_text(text, message);
    if (value.type == 2 || value.type == 8) release_value(&value);
}
unsigned native_collision(int key) {
    using Device = uint32_t(*)(uintptr_t);
    const auto device = method<Device>(input_object, 0x58);
    if (!device) return 6;
    const auto which = device(input_object);
    if (which >= 4) return 6;
    const auto command = bound_command(which, 0, key, nullptr);
    char first = 0;
    if (!command || !read(reinterpret_cast<uintptr_t>(command), first)) return 6;
    return first ? 3u : 0u;
}
unsigned write_error(controls::WriteResult result) {
    switch (result) {
    case controls::WriteResult::saved: return 0;
    case controls::WriteResult::invalid: return 1;
    case controls::WriteResult::conflict: return 2;
    case controls::WriteResult::rollback_error: return 5;
    default: return 4;
    }
}
int subaction(const void* raw) {
    const auto action = reinterpret_cast<uintptr_t>(raw);
    int32_t kind = 0, count = 0;
    uintptr_t values = 0;
    Value value{};
    if (!read(action, kind) || kind != 1 || !read(action + 0x10, count) || count < 1 ||
        !read(action + 8, values) || !read(values, value) || value.type != 5) return -1;
    return static_cast<int32_t>(value.payload);
}
bool action_detour(uintptr_t popup, const void* action) {
    const auto row = selected_row(popup);
    const auto index = row ? action_index(row + 0x2a0) : -1;
    const auto choice = subaction(action);
    if (index < 0 || choice < 0 || choice > 2) return original_action(popup, action);
    if (!enabled.load(std::memory_order_acquire)) return true;
    if (choice != 1 && action_index(popup + 0x158) >= 0) end_capture(popup);
    if (choice == 1) {
        errors[index] = 0;
        begin_capture(popup, 0);
    } else if (choice == 2) {
        errors[index] = write_error(controls::write(queue_directory, static_cast<unsigned>(index), 0));
        refresh(true);
        render_ap(row);
    } else {
        unsigned why = 0;
        for (const auto key : controls::reset_defaults) {
            const auto native = static_cast<int>(MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC));
            why = native ? native_collision(native) : 6;
            if (why) break;
        }
        if (!why) why = write_error(controls::reset(queue_directory));
        errors[0] = errors[1] = why;
        refresh(true);
        render_ap(row);
    }
    return true;
}
bool key_detour(uintptr_t popup, const void* event) {
    const auto index = action_index(popup + 0x158);
    if (index < 0) return original_key(popup, event);
    int32_t words[3]{};
    engine::LocalMemory memory;
    if (memory.copy(reinterpret_cast<uintptr_t>(event), words, sizeof(words)).reason) return true;
    if (!enabled.load(std::memory_order_acquire)) { end_capture(popup); return true; }
    if (words[0] != 1 || !words[2]) return true;
    if (words[1] == 1 || words[1] == 0x101) return original_key(popup, event);
    const bool modified = ((GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_CONTROL) |
        GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
    const auto vk = modified ? -1 : controls::from_native_key(words[1]);
    unsigned why = vk < 1 ? 1u : native_collision(words[1]);
    if (!why) why = write_error(controls::write(queue_directory, static_cast<unsigned>(index), vk));
    errors[index] = why;
    if (why) prompt(popup, messages[why]);
    else {
        end_capture(popup);
        refresh(true);
        if (const auto row = selected_row(popup)) render_ap(row);
    }
    return true;
}
void append_rows(uintptr_t popup, uintptr_t list) {
    using Init = void(*)(uintptr_t, uintptr_t);
    using Add = bool(*)(uintptr_t, int, uintptr_t, int, int);
    const auto add = method<Add>(list, 0xd8);
    uintptr_t swf = 0;
    if (!add || !read(popup + 0x10, swf) || !swf) return;
    add_group(list, 4, player_strings::controls_group);
    for (int i = 0; i < 2; ++i) {
        const auto storage = allocate(0x358);
        if (!storage) break;
        const auto row = construct(storage);
        configure(row, commands[i], "", "", 0, 0);
        put<int32_t>(row, 0x350, 1);
        put<int32_t>(row, 0x354, 2);
        const auto init = method<Init>(row, 0x18);
        if (init) init(row, swf);
        if (!init || !add(list, 4, row, 0, -1)) {
            if (const auto destroy = method<Destroy>(row, 0)) destroy(row, 1);
            break;
        }
    }
}
void finalize_detour(uintptr_t list) {
    uintptr_t expected = 0;
    uint8_t demon = 1;
    if (building_popup && enabled.load(std::memory_order_acquire) &&
        read(building_popup + 0x100, expected) && expected == list &&
        read(building_popup + 0x130, demon) && !demon) {
        const auto popup = building_popup;
        building_popup = 0;
        append_rows(popup, list);
    }
    original_finalize(list);
}
void build_detour(uintptr_t popup) {
    const auto previous = building_popup;
    building_popup = enabled.load(std::memory_order_acquire) ? popup : 0;
    __try { original_build(popup); }
    __finally { building_popup = previous; }
}
void show_detour(uintptr_t popup, int mode) {
    visible_popup.store(popup, std::memory_order_release);
    errors[0] = errors[1] = 0;
    next_read = 0;
    original_show(popup, mode);
}
void hide_detour(uintptr_t popup) {
    original_hide(popup);
    auto expected = popup;
    visible_popup.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
}
uintptr_t destroy_detour(uintptr_t popup, uint32_t flags) {
    auto expected = popup;
    visible_popup.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    return original_destroy(popup, flags);
}
void clear_chat(uintptr_t chat) {
    auto expected = chat;
    editing_chat.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
}
void chat_begin_detour(uintptr_t chat) {
    editing_chat.store(chat, std::memory_order_release);
    original_chat_begin(chat);
    uint8_t editing = 0;
    if (read(chat + 0x188, editing) && !editing) clear_chat(chat);
}
void chat_end_detour(uintptr_t chat) {
    original_chat_end(chat);
    uint8_t editing = 0;
    if (read(chat + 0x188, editing) && !editing) clear_chat(chat);
}
uintptr_t chat_destroy_detour(uintptr_t chat, uint32_t flags) {
    clear_chat(chat);
    return original_chat_destroy(chat, flags);
}
void build_detour_entry(uintptr_t popup) {
    const auto lease = hook_set.enter(0);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_build)>(lease.original)(popup);
        return build_detour(popup);
    } __finally { hook_set.leave(lease); }
}

void finalize_detour_entry(uintptr_t list) {
    const auto lease = hook_set.enter(1);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_finalize)>(lease.original)(list);
        return finalize_detour(list);
    } __finally { hook_set.leave(lease); }
}

void render_detour_entry(uintptr_t row) {
    const auto lease = hook_set.enter(2);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_render)>(lease.original)(row);
        return render_detour(row);
    } __finally { hook_set.leave(lease); }
}

bool action_detour_entry(uintptr_t popup, const void* action) {
    const auto lease = hook_set.enter(3);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_action)>(lease.original)(popup, action);
        return action_detour(popup, action);
    } __finally { hook_set.leave(lease); }
}

bool key_detour_entry(uintptr_t popup, const void* event) {
    const auto lease = hook_set.enter(4);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_key)>(lease.original)(popup, event);
        return key_detour(popup, event);
    } __finally { hook_set.leave(lease); }
}

void show_detour_entry(uintptr_t popup, int mode) {
    const auto lease = hook_set.enter(5);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_show)>(lease.original)(popup, mode);
        return show_detour(popup, mode);
    } __finally { hook_set.leave(lease); }
}

void hide_detour_entry(uintptr_t popup) {
    const auto lease = hook_set.enter(6);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_hide)>(lease.original)(popup);
        return hide_detour(popup);
    } __finally { hook_set.leave(lease); }
}

uintptr_t destroy_detour_entry(uintptr_t popup, uint32_t flags) {
    const auto lease = hook_set.enter(7);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_destroy)>(lease.original)(popup, flags);
        return destroy_detour(popup, flags);
    } __finally { hook_set.leave(lease); }
}

void chat_begin_detour_entry(uintptr_t chat) {
    const auto lease = hook_set.enter(8);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_chat_begin)>(lease.original)(chat);
        return chat_begin_detour(chat);
    } __finally { hook_set.leave(lease); }
}

void chat_end_detour_entry(uintptr_t chat) {
    const auto lease = hook_set.enter(9);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_chat_end)>(lease.original)(chat);
        return chat_end_detour(chat);
    } __finally { hook_set.leave(lease); }
}

uintptr_t chat_destroy_detour_entry(uintptr_t chat, uint32_t flags) {
    const auto lease = hook_set.enter(10);
    __try {
        if (!lease.committed) return reinterpret_cast<decltype(original_chat_destroy)>(lease.original)(chat, flags);
        return chat_destroy_detour(chat, flags);
    } __finally { hook_set.leave(lease); }
}

using builds::Site;
const auto& sites = builds::steam_20260818().controls;
constexpr unsigned hook_sites[]{0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26};
native::Target target(uintptr_t base, const Site& site) {
    native::Target t{};
    t.address = base + site.rva;
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t n = 0; n < t.bytes.size(); ++n)
        t.bytes[n] = static_cast<uint8_t>((nibble(site.prefix[n * 2]) << 4) | nibble(site.prefix[n * 2 + 1]));
    return t;
}
}
bool install(const engine::Binding& binding, HANDLE stop_event) {
    if (!save::session().campaign_requested()) return true;
    engine::LocalMemory memory;
    const auto deadline = GetTickCount64() + 10000;
#ifdef SC_NATIVE_TESTING
    if (testing::real_hook_targets())
#endif
    for (size_t i = 0; i < std::size(sites); ++i) {
        const auto expected = target(binding.image.base, sites[i]);
        if (i < 8 || i >= 24) {
            if (native::validate_target(memory, binding.image, expected, stop_event, deadline)) return false;
        } else {
            std::array<uint8_t,32> actual{};
            if (!binding.image.contains(sites[i].rva, actual.size(), IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ, 0) ||
                memory.copy(expected.address, actual.data(), actual.size()).reason || actual != expected.bytes) return false;
        }
    }
    const auto count = GetFullPathNameW(L"base\\ap_queue", 520, queue_directory, nullptr);
    if (!count || count >= 520) return false;
    const auto at = [&](unsigned i) { return binding.image.base + sites[i].rva; };
    allocate = reinterpret_cast<Allocate>(at(8));
    construct = reinterpret_cast<Construct>(at(9));
    configure = reinterpret_cast<Configure>(at(10));
    add_group = reinterpret_cast<Group>(at(11));
    begin_capture = reinterpret_cast<Capture>(at(12));
    end_capture = reinterpret_cast<Update>(at(13));
    assign_string = reinterpret_cast<Assign>(at(14));
    widget_text = reinterpret_cast<WidgetText>(at(15));
    assign_text = reinterpret_cast<Assign>(at(16));
    bound_command = reinterpret_cast<Bound>(at(17));
    lookup = reinterpret_cast<Lookup>(at(18));
    text_instance = reinterpret_cast<Text>(at(19));
    release_value = reinterpret_cast<Release>(at(20));
    input_object = binding.image.base + 0x440fc10;
    console_object = binding.image.base + 0x47e3800;
    menu_pointer = binding.image.base + 0x46ae140;
    menu_active = reinterpret_cast<MenuActive>(at(22));
    void* detours[] = {reinterpret_cast<void*>(build_detour_entry), reinterpret_cast<void*>(finalize_detour_entry),
        reinterpret_cast<void*>(render_detour_entry), reinterpret_cast<void*>(action_detour_entry),
        reinterpret_cast<void*>(key_detour_entry), reinterpret_cast<void*>(show_detour_entry),
        reinterpret_cast<void*>(hide_detour_entry), reinterpret_cast<void*>(destroy_detour_entry),
        reinterpret_cast<void*>(chat_begin_detour_entry), reinterpret_cast<void*>(chat_end_detour_entry),
        reinterpret_cast<void*>(chat_destroy_detour_entry)};
    void** originals[] = {reinterpret_cast<void**>(&original_build), reinterpret_cast<void**>(&original_finalize),
        reinterpret_cast<void**>(&original_render), reinterpret_cast<void**>(&original_action),
        reinterpret_cast<void**>(&original_key), reinterpret_cast<void**>(&original_show),
        reinterpret_cast<void**>(&original_hide), reinterpret_cast<void**>(&original_destroy),
        reinterpret_cast<void**>(&original_chat_begin), reinterpret_cast<void**>(&original_chat_end),
        reinterpret_cast<void**>(&original_chat_destroy)};
    std::vector<hooks::HookSpec> specs;
    for (size_t i = 0; i < std::size(hook_sites); ++i)
        specs.push_back({at(hook_sites[i]), detours[i], originals[i]});
    if (!hook_set.install(specs.data(), specs.size(), stop_event)) return false;
    enabled.store(true, std::memory_order_release);
    return true;
}
void stop() { enabled.store(false, std::memory_order_release); }
bool input_inhibited() noexcept {
    if (visible_popup.load(std::memory_order_acquire) || editing_chat.load(std::memory_order_acquire)) return true;
    if (!console_object) return false;
    __try {
        if (*reinterpret_cast<const uint8_t*>(console_object + 0xd0) ||
            *reinterpret_cast<const uint8_t*>(console_object + 0x608108)) return true;
        const auto menu = *reinterpret_cast<const uintptr_t*>(menu_pointer);
        return menu && menu_active(menu);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}
bool recovery_input_allowed() noexcept {
    if (!enabled.load(std::memory_order_acquire) || visible_popup.load(std::memory_order_acquire) ||
        editing_chat.load(std::memory_order_acquire)) return false;
    __try {
        const auto menu = *reinterpret_cast<const uintptr_t*>(menu_pointer);
        return !menu || !menu_active(menu);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
