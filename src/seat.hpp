// Seat: клавиатура + указатель, фокус, маршрутизация к клиентам.
#pragma once

#include <cstdint>
#include <vector>

#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>

struct Server;
struct Surface;
struct Toplevel;

struct Seat {
    Server* server = nullptr;

    std::vector<wl_resource*> keyboards; // все wl_keyboard всех клиентов
    std::vector<wl_resource*> pointers;

    xkb_context* xkb = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* xkb_state_ = nullptr;
    int keymap_fd = -1;
    uint32_t keymap_size = 0;

    Toplevel* kb_focus = nullptr;
    Surface* ptr_focus = nullptr; // поверхность (в т.ч. суб-), куда идут pointer-события
    int buttons_down = 0;         // implicit grab, пока >0 — ptr_focus залочен

    double cur_x = 0, cur_y = 0; // координаты output
    std::vector<uint32_t> pressed_keys;
    uint32_t mods_depressed = 0, mods_latched = 0, mods_locked = 0, mods_group = 0;

    bool init(Server&);
    void finish();

    // события от бэкенда (инъекция/libinput)
    void handle_motion(double x, double y);
    void handle_button(uint32_t button, bool pressed);
    void handle_key(uint32_t evdev_code, bool pressed);
    void handle_scroll(double value); // в делениях колеса

    // жизненный цикл окон
    void focus_toplevel(Toplevel*); // клавиатурный фокус (focus-on-map, click-to-focus)
    void toplevel_gone(Toplevel*);
    void surface_gone(Surface*);

private:
    void send_keymap(wl_resource* kb);
    void update_modifiers();
    void kb_send_to_focus_client(uint32_t key, bool pressed, uint32_t serial, uint32_t time);
    Surface* pick_pointer_target();
    void pointer_set_focus(Surface*, double sx, double sy);
    bool same_client(wl_resource* res, Toplevel* t);
    bool same_client_s(wl_resource* res, Surface* s);
};

void seat_create_global(Server&);
