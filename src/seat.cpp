#include "seat.hpp"

#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wayland-server-protocol.h>

#include "imgui.h"
#include "server.hpp"

namespace {

constexpr uint32_t kSeatVersion = 5;

void res_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

Seat* seat_of(wl_resource* res) { return (Seat*)wl_resource_get_user_data(res); }

// --- wl_pointer / wl_keyboard / wl_touch ресурсы ---

void pointer_set_cursor(wl_client*, wl_resource*, uint32_t, wl_resource*, int32_t, int32_t) {
    // курсоры клиентов — M3 (в headless рисовать некуда)
}

const struct wl_pointer_interface pointer_impl = {
    .set_cursor = pointer_set_cursor,
    .release = res_destroy,
};
const struct wl_keyboard_interface keyboard_impl = {.release = res_destroy};
const struct wl_touch_interface touch_impl = {.release = res_destroy};

void pointer_resource_destroyed(wl_resource* res) {
    Seat* seat = seat_of(res);
    std::erase(seat->pointers, res);
}

void keyboard_resource_destroyed(wl_resource* res) {
    Seat* seat = seat_of(res);
    std::erase(seat->keyboards, res);
}

void seat_get_pointer(wl_client* client, wl_resource* res, uint32_t id) {
    Seat* seat = seat_of(res);
    wl_resource* p =
        wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(res), id);
    if (!p) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(p, &pointer_impl, seat, pointer_resource_destroyed);
    seat->pointers.push_back(p);
}

void seat_get_keyboard(wl_client* client, wl_resource* res, uint32_t id) {
    Seat* seat = seat_of(res);
    wl_resource* k =
        wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(res), id);
    if (!k) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(k, &keyboard_impl, seat, keyboard_resource_destroyed);
    seat->keyboards.push_back(k);

    wl_keyboard_send_keymap(k, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, seat->keymap_fd,
                            seat->keymap_size);
    if (wl_resource_get_version(k) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
        wl_keyboard_send_repeat_info(k, 25, 600);

    // если фокус уже у этого клиента — новая клавиатура должна получить enter
    Seat& s = *seat;
    if (s.kb_focus && s.kb_focus->xdg && s.kb_focus->xdg->surface &&
        wl_resource_get_client(s.kb_focus->xdg->surface->res) == client) {
        wl_array keys;
        wl_array_init(&keys);
        for (uint32_t kc : s.pressed_keys)
            *(uint32_t*)wl_array_add(&keys, sizeof(uint32_t)) = kc;
        wl_keyboard_send_enter(k, wl_display_next_serial(s.server->display),
                               s.kb_focus->xdg->surface->res, &keys);
        wl_array_release(&keys);
        wl_keyboard_send_modifiers(k, wl_display_next_serial(s.server->display),
                                   s.mods_depressed, s.mods_latched, s.mods_locked, s.mods_group);
    }
}

void seat_get_touch(wl_client* client, wl_resource* res, uint32_t id) {
    wl_resource* t =
        wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(res), id);
    if (t) wl_resource_set_implementation(t, &touch_impl, nullptr, nullptr);
}

const struct wl_seat_interface seat_impl = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = res_destroy,
};

void seat_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    wl_resource* res = wl_resource_create(client, &wl_seat_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &seat_impl, data, nullptr);
    wl_seat_send_capabilities(res, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
    if (version >= WL_SEAT_NAME_SINCE_VERSION) wl_seat_send_name(res, "seat0");
}

} // namespace

bool Seat::init(Server& srv) {
    server = &srv;
    xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb) return false;
    keymap = xkb_keymap_new_from_names(xkb, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) return false;
    xkb_state_ = xkb_state_new(keymap);

    char* str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    keymap_size = (uint32_t)strlen(str) + 1;
    keymap_fd = memfd_create("imway-keymap", 0);
    if (keymap_fd < 0 || write(keymap_fd, str, keymap_size) != (ssize_t)keymap_size) {
        std::fprintf(stderr, "seat: не удалось создать keymap fd\n");
        free(str);
        return false;
    }
    free(str);
    return true;
}

void Seat::finish() {
    if (keymap_fd >= 0) close(keymap_fd);
    if (xkb_state_) xkb_state_unref(xkb_state_);
    if (keymap) xkb_keymap_unref(keymap);
    if (xkb) xkb_context_unref(xkb);
}

bool Seat::same_client(wl_resource* res, Toplevel* t) {
    return t && t->xdg && t->xdg->surface &&
           wl_resource_get_client(res) == wl_resource_get_client(t->xdg->surface->res);
}

Toplevel* Seat::pick_pointer_target() {
    // hovered-флаги выставлены ImGui в последнем кадре (учитывают z-order)
    for (Toplevel* t : server->toplevels)
        if (t->mapped && t->hovered) return t;
    return nullptr;
}

void Seat::pointer_set_focus(Toplevel* t, double sx, double sy) {
    if (ptr_focus == t) return;
    if (ptr_focus && ptr_focus->xdg && ptr_focus->xdg->surface) {
        uint32_t serial = wl_display_next_serial(server->display);
        for (wl_resource* p : pointers)
            if (same_client(p, ptr_focus)) {
                wl_pointer_send_leave(p, serial, ptr_focus->xdg->surface->res);
                if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                    wl_pointer_send_frame(p);
            }
    }
    ptr_focus = t;
    if (t && t->xdg && t->xdg->surface) {
        uint32_t serial = wl_display_next_serial(server->display);
        for (wl_resource* p : pointers)
            if (same_client(p, t)) {
                wl_pointer_send_enter(p, serial, t->xdg->surface->res,
                                      wl_fixed_from_double(sx), wl_fixed_from_double(sy));
                if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                    wl_pointer_send_frame(p);
            }
    }
}

void Seat::handle_motion(double x, double y) {
    cur_x = x;
    cur_y = y;
    ImGui::GetIO().AddMousePosEvent((float)x, (float)y);

    Toplevel* target = buttons_down > 0 ? ptr_focus : pick_pointer_target();
    if (target != ptr_focus) {
        double sx = target ? x - target->img_x : 0, sy = target ? y - target->img_y : 0;
        pointer_set_focus(target, sx, sy);
        return;
    }
    if (!ptr_focus) return;

    double sx = x - ptr_focus->img_x;
    double sy = y - ptr_focus->img_y;
    uint32_t t = now_msec();
    for (wl_resource* p : pointers)
        if (same_client(p, ptr_focus)) {
            wl_pointer_send_motion(p, t, wl_fixed_from_double(sx), wl_fixed_from_double(sy));
            if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                wl_pointer_send_frame(p);
        }
}

void Seat::handle_button(uint32_t button, bool pressed) {
    int imgui_btn = button == BTN_LEFT ? 0 : button == BTN_RIGHT ? 1 : 2;
    ImGui::GetIO().AddMouseButtonEvent(imgui_btn, pressed);

    if (pressed && ptr_focus) focus_toplevel(ptr_focus); // click-to-focus

    if (ptr_focus) {
        uint32_t serial = wl_display_next_serial(server->display);
        uint32_t t = now_msec();
        for (wl_resource* p : pointers)
            if (same_client(p, ptr_focus)) {
                wl_pointer_send_button(p, serial, t, button,
                                       pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                               : WL_POINTER_BUTTON_STATE_RELEASED);
                if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                    wl_pointer_send_frame(p);
            }
    }
    buttons_down += pressed ? 1 : -1;
    if (buttons_down < 0) buttons_down = 0;
}

void Seat::handle_scroll(double value) {
    ImGui::GetIO().AddMouseWheelEvent(0.f, (float)-value);
    if (!ptr_focus) return;
    uint32_t t = now_msec();
    for (wl_resource* p : pointers)
        if (same_client(p, ptr_focus)) {
            wl_pointer_send_axis(p, t, WL_POINTER_AXIS_VERTICAL_SCROLL,
                                 wl_fixed_from_double(value * 15.0));
            if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION)
                wl_pointer_send_frame(p);
        }
}

void Seat::update_modifiers() {
    uint32_t dep = xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_DEPRESSED);
    uint32_t lat = xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_LATCHED);
    uint32_t lock = xkb_state_serialize_mods(xkb_state_, XKB_STATE_MODS_LOCKED);
    uint32_t grp = xkb_state_serialize_layout(xkb_state_, XKB_STATE_LAYOUT_EFFECTIVE);
    if (dep == mods_depressed && lat == mods_latched && lock == mods_locked && grp == mods_group)
        return;
    mods_depressed = dep;
    mods_latched = lat;
    mods_locked = lock;
    mods_group = grp;
    if (!kb_focus) return;
    uint32_t serial = wl_display_next_serial(server->display);
    for (wl_resource* k : keyboards)
        if (same_client(k, kb_focus))
            wl_keyboard_send_modifiers(k, serial, dep, lat, lock, grp);
}

void Seat::handle_key(uint32_t code, bool pressed) {
    xkb_state_update_key(xkb_state_, code + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

    if (pressed)
        pressed_keys.push_back(code);
    else
        std::erase(pressed_keys, code);

    if (kb_focus) {
        uint32_t serial = wl_display_next_serial(server->display);
        uint32_t t = now_msec();
        for (wl_resource* k : keyboards)
            if (same_client(k, kb_focus))
                wl_keyboard_send_key(k, serial, t, code,
                                     pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                             : WL_KEYBOARD_KEY_STATE_RELEASED);
    }
    update_modifiers();
}

void Seat::focus_toplevel(Toplevel* t) {
    if (kb_focus == t) return;

    if (kb_focus && kb_focus->xdg && kb_focus->xdg->surface) {
        uint32_t serial = wl_display_next_serial(server->display);
        for (wl_resource* k : keyboards)
            if (same_client(k, kb_focus))
                wl_keyboard_send_leave(k, serial, kb_focus->xdg->surface->res);
    }
    kb_focus = t;
    if (t && t->xdg && t->xdg->surface) {
        uint32_t serial = wl_display_next_serial(server->display);
        wl_array keys;
        wl_array_init(&keys);
        for (uint32_t kc : pressed_keys) *(uint32_t*)wl_array_add(&keys, sizeof(uint32_t)) = kc;
        for (wl_resource* k : keyboards)
            if (same_client(k, t)) {
                wl_keyboard_send_enter(k, serial, t->xdg->surface->res, &keys);
                wl_keyboard_send_modifiers(k, wl_display_next_serial(server->display),
                                           mods_depressed, mods_latched, mods_locked, mods_group);
            }
        wl_array_release(&keys);
        std::printf("imway: фокус → «%s»\n", t->title.c_str());
    }
}

void Seat::toplevel_gone(Toplevel* t) {
    if (ptr_focus == t) {
        ptr_focus = nullptr;
        buttons_down = 0;
    }
    if (kb_focus == t) {
        kb_focus = nullptr;
        // отдать фокус последнему замапленному
        for (auto it = server->toplevels.rbegin(); it != server->toplevels.rend(); ++it)
            if (*it != t && (*it)->mapped) {
                focus_toplevel(*it);
                break;
            }
    }
}

void seat_create_global(Server& server) {
    wl_global_create(server.display, &wl_seat_interface, kSeatVersion, server.seat, seat_bind);
}
