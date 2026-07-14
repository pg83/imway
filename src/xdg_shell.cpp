// xdg_wm_base / xdg_surface / xdg_toplevel — минимум для map'а toplevel-окна.

#include <cstdio>

#include <wayland-server-protocol.h>
#include <xdg-shell-server-protocol.h>

#include "renderer.hpp"
#include "seat.hpp"
#include "server.hpp"

namespace {

// --- xdg_toplevel ---

void toplevel_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }
void toplevel_set_parent(wl_client*, wl_resource*, wl_resource*) {}

void toplevel_set_title(wl_client*, wl_resource* res, const char* title) {
    ((Toplevel*)wl_resource_get_user_data(res))->title = title;
}

void toplevel_set_app_id(wl_client*, wl_resource* res, const char* app_id) {
    ((Toplevel*)wl_resource_get_user_data(res))->app_id = app_id;
}

void toplevel_show_window_menu(wl_client*, wl_resource*, wl_resource*, uint32_t, int32_t, int32_t) {}
void toplevel_move(wl_client*, wl_resource*, wl_resource*, uint32_t) {}
void toplevel_resize(wl_client*, wl_resource*, wl_resource*, uint32_t, uint32_t) {}
void toplevel_set_max_size(wl_client*, wl_resource*, int32_t, int32_t) {}
void toplevel_set_min_size(wl_client*, wl_resource*, int32_t, int32_t) {}
void toplevel_set_maximized(wl_client*, wl_resource*) {}
void toplevel_unset_maximized(wl_client*, wl_resource*) {}
void toplevel_set_fullscreen(wl_client*, wl_resource*, wl_resource*) {}
void toplevel_unset_fullscreen(wl_client*, wl_resource*) {}
void toplevel_set_minimized(wl_client*, wl_resource*) {}

const struct xdg_toplevel_interface toplevel_impl = {
    .destroy = toplevel_destroy,
    .set_parent = toplevel_set_parent,
    .set_title = toplevel_set_title,
    .set_app_id = toplevel_set_app_id,
    .show_window_menu = toplevel_show_window_menu,
    .move = toplevel_move,
    .resize = toplevel_resize,
    .set_max_size = toplevel_set_max_size,
    .set_min_size = toplevel_set_min_size,
    .set_maximized = toplevel_set_maximized,
    .unset_maximized = toplevel_unset_maximized,
    .set_fullscreen = toplevel_set_fullscreen,
    .unset_fullscreen = toplevel_unset_fullscreen,
    .set_minimized = toplevel_set_minimized,
};

void toplevel_resource_destroyed(wl_resource* res) {
    auto* t = (Toplevel*)wl_resource_get_user_data(res);
    if (t->server->seat) t->server->seat->toplevel_gone(t);
    if (t->xdg) t->xdg->toplevel = nullptr;
    t->server->toplevels.remove(t);
    std::printf("imway: toplevel «%s» уничтожен\n", t->title.c_str());
    delete t;
}

// --- xdg_surface ---

void xdg_surface_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void send_configure(XdgSurface& xs) {
    if (xs.toplevel) {
        wl_array states;
        wl_array_init(&states);
        xdg_toplevel_send_configure(xs.toplevel->res, 0, 0, &states);
        wl_array_release(&states);
    } else if (xs.popup) {
        Popup& p = *xs.popup;
        xdg_popup_send_configure(p.res, p.x, p.y, p.w, p.h);
    }
    xdg_surface_send_configure(xs.res, wl_display_next_serial(xs.server->display));
    xs.initial_configure_sent = true;
}

void xdg_surface_get_toplevel(wl_client* client, wl_resource* res, uint32_t id) {
    auto* xs = (XdgSurface*)wl_resource_get_user_data(res);
    wl_resource* tres =
        wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(res), id);
    if (!tres) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* t = new Toplevel();
    t->server = xs->server;
    t->res = tres;
    t->xdg = xs;
    t->id = xs->server->next_toplevel_id++;
    xs->toplevel = t;
    xs->server->toplevels.push_back(t);
    wl_resource_set_implementation(tres, &toplevel_impl, t, toplevel_resource_destroyed);
}

// определение ниже: нужны Positioner и popup_impl из секции попапов
void xdg_surface_get_popup(wl_client* client, wl_resource* res, uint32_t id,
                           wl_resource* parent_res, wl_resource* positioner_res);

void xdg_surface_set_window_geometry(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {
    // M1: рисуем буфер целиком; кроп по geometry — M2
}

void xdg_surface_ack_configure(wl_client*, wl_resource* res, uint32_t) {
    ((XdgSurface*)wl_resource_get_user_data(res))->acked = true;
}

} // namespace

void xdg_toplevel_configure_size(Toplevel& t, int w, int h) {
    wl_array states;
    wl_array_init(&states);
    xdg_toplevel_send_configure(t.res, w, h, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(t.xdg->res, wl_display_next_serial(t.server->display));
    t.cfg_w = w;
    t.cfg_h = h;
    std::printf("imway: configure «%s» → %dx%d\n", t.title.c_str(), w, h);
}

namespace {

const struct xdg_surface_interface xdg_surface_impl = {
    .destroy = xdg_surface_destroy,
    .get_toplevel = xdg_surface_get_toplevel,
    .get_popup = xdg_surface_get_popup,
    .set_window_geometry = xdg_surface_set_window_geometry,
    .ack_configure = xdg_surface_ack_configure,
};

void xdg_surface_resource_destroyed(wl_resource* res) {
    auto* xs = (XdgSurface*)wl_resource_get_user_data(res);
    if (xs->surface) xs->surface->xdg = nullptr;
    if (xs->toplevel) xs->toplevel->xdg = nullptr;
    if (xs->popup) xs->popup->xdg = nullptr;
    delete xs;
}

// --- xdg_positioner ---

struct Positioner {
    int w = 0, h = 0;                       // set_size
    int ax = 0, ay = 0, aw = 0, ah = 0;     // anchor_rect
    uint32_t anchor = XDG_POSITIONER_ANCHOR_NONE;
    uint32_t gravity = XDG_POSITIONER_GRAVITY_NONE;
    int dx = 0, dy = 0; // offset

    // левый-верхний угол попапа в координатах родителя
    void place(int& out_x, int& out_y) const {
        int px = ax, py = ay; // якорная точка на anchor_rect
        switch (anchor) {
        case XDG_POSITIONER_ANCHOR_TOP: px += aw / 2; break;
        case XDG_POSITIONER_ANCHOR_BOTTOM: px += aw / 2; py += ah; break;
        case XDG_POSITIONER_ANCHOR_LEFT: py += ah / 2; break;
        case XDG_POSITIONER_ANCHOR_RIGHT: px += aw; py += ah / 2; break;
        case XDG_POSITIONER_ANCHOR_TOP_LEFT: break;
        case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT: py += ah; break;
        case XDG_POSITIONER_ANCHOR_TOP_RIGHT: px += aw; break;
        case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT: px += aw; py += ah; break;
        default: px += aw / 2; py += ah / 2; break; // NONE = центр
        }
        // gravity: в какую сторону попап растёт от якоря
        switch (gravity) {
        case XDG_POSITIONER_GRAVITY_TOP: px -= w / 2; py -= h; break;
        case XDG_POSITIONER_GRAVITY_BOTTOM: px -= w / 2; break;
        case XDG_POSITIONER_GRAVITY_LEFT: px -= w; py -= h / 2; break;
        case XDG_POSITIONER_GRAVITY_RIGHT: py -= h / 2; break;
        case XDG_POSITIONER_GRAVITY_TOP_LEFT: px -= w; py -= h; break;
        case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT: px -= w; break;
        case XDG_POSITIONER_GRAVITY_TOP_RIGHT: py -= h; break;
        case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT: break;
        default: px -= w / 2; py -= h / 2; break; // NONE = центр
        }
        out_x = px + dx;
        out_y = py + dy;
    }
};

Positioner* positioner_from(wl_resource* res) {
    return (Positioner*)wl_resource_get_user_data(res);
}

void positioner_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void positioner_set_size(wl_client*, wl_resource* res, int32_t w, int32_t h) {
    positioner_from(res)->w = w;
    positioner_from(res)->h = h;
}

void positioner_set_anchor_rect(wl_client*, wl_resource* res, int32_t x, int32_t y, int32_t w,
                                int32_t h) {
    Positioner* p = positioner_from(res);
    p->ax = x;
    p->ay = y;
    p->aw = w;
    p->ah = h;
}

void positioner_set_anchor(wl_client*, wl_resource* res, uint32_t a) {
    positioner_from(res)->anchor = a;
}

void positioner_set_gravity(wl_client*, wl_resource* res, uint32_t g) {
    positioner_from(res)->gravity = g;
}

void positioner_set_constraint_adjustment(wl_client*, wl_resource*, uint32_t) {
    // слайды/флипы у краёв — позже; в ImGui-окне попап и так виден
}

void positioner_set_offset(wl_client*, wl_resource* res, int32_t x, int32_t y) {
    positioner_from(res)->dx = x;
    positioner_from(res)->dy = y;
}

void positioner_set_reactive(wl_client*, wl_resource*) {}
void positioner_set_parent_size(wl_client*, wl_resource*, int32_t, int32_t) {}
void positioner_set_parent_configure(wl_client*, wl_resource*, uint32_t) {}

const struct xdg_positioner_interface positioner_impl = {
    .destroy = positioner_destroy,
    .set_size = positioner_set_size,
    .set_anchor_rect = positioner_set_anchor_rect,
    .set_anchor = positioner_set_anchor,
    .set_gravity = positioner_set_gravity,
    .set_constraint_adjustment = positioner_set_constraint_adjustment,
    .set_offset = positioner_set_offset,
    .set_reactive = positioner_set_reactive,
    .set_parent_size = positioner_set_parent_size,
    .set_parent_configure = positioner_set_parent_configure,
};

void positioner_resource_destroyed(wl_resource* res) { delete positioner_from(res); }

// --- xdg_popup ---

void popup_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void popup_grab(wl_client*, wl_resource* res, wl_resource* /*seat*/, uint32_t /*serial*/) {
    auto* p = (Popup*)wl_resource_get_user_data(res);
    p->grab = true;
}

void popup_reposition(wl_client*, wl_resource* res, wl_resource* positioner, uint32_t token) {
    auto* p = (Popup*)wl_resource_get_user_data(res);
    Positioner* pos = positioner_from(positioner);
    pos->place(p->x, p->y);
    p->w = pos->w;
    p->h = pos->h;
    xdg_popup_send_repositioned(res, token);
    xdg_popup_send_configure(res, p->x, p->y, p->w, p->h);
    xdg_surface_send_configure(p->xdg->res, wl_display_next_serial(p->server->display));
    p->server->needs_frame = true;
}

const struct xdg_popup_interface popup_impl = {
    .destroy = popup_destroy,
    .grab = popup_grab,
    .reposition = popup_reposition,
};

void popup_resource_destroyed(wl_resource* res) {
    auto* p = (Popup*)wl_resource_get_user_data(res);
    if (p->server->seat) p->server->seat->popup_gone(p);
    if (p->xdg) p->xdg->popup = nullptr;
    p->server->popups.remove(p);
    p->server->needs_frame = true;
    delete p;
}

void xdg_surface_get_popup(wl_client* client, wl_resource* res, uint32_t id,
                           wl_resource* parent_res, wl_resource* positioner_res) {
    auto* xs = (XdgSurface*)wl_resource_get_user_data(res);
    if (!parent_res) {
        wl_resource_post_error(res, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                               "попап без родителя не поддержан");
        return;
    }
    auto* parent_xs = (XdgSurface*)wl_resource_get_user_data(parent_res);
    wl_resource* pres =
        wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(res), id);
    if (!pres) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* p = new Popup();
    p->server = xs->server;
    p->res = pres;
    p->xdg = xs;
    p->parent = parent_xs->surface;
    Positioner* pos = positioner_from(positioner_res);
    pos->place(p->x, p->y);
    p->w = pos->w;
    p->h = pos->h;
    xs->popup = p;
    xs->server->popups.push_back(p);
    wl_resource_set_implementation(pres, &popup_impl, p, popup_resource_destroyed);
}

// --- xdg_wm_base ---

void wm_base_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void wm_base_create_positioner(wl_client* client, wl_resource* res, uint32_t id) {
    wl_resource* pres =
        wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(res), id);
    if (!pres) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(pres, &positioner_impl, new Positioner(),
                                   positioner_resource_destroyed);
}

void wm_base_get_xdg_surface(wl_client* client, wl_resource* res, uint32_t id,
                             wl_resource* surface_res) {
    auto* server = (Server*)wl_resource_get_user_data(res);
    auto* surface = (Surface*)wl_resource_get_user_data(surface_res);
    wl_resource* xres =
        wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(res), id);
    if (!xres) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* xs = new XdgSurface();
    xs->server = server;
    xs->res = xres;
    xs->surface = surface;
    surface->xdg = xs;
    wl_resource_set_implementation(xres, &xdg_surface_impl, xs, xdg_surface_resource_destroyed);
}

void wm_base_pong(wl_client*, wl_resource*, uint32_t) {}

const struct xdg_wm_base_interface wm_base_impl = {
    .destroy = wm_base_destroy,
    .create_positioner = wm_base_create_positioner,
    .get_xdg_surface = wm_base_get_xdg_surface,
    .pong = wm_base_pong,
};

void wm_base_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    wl_resource* res = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &wm_base_impl, data, nullptr);
}

} // namespace

void xdg_handle_commit(Surface& s) {
    XdgSurface* xs = s.xdg;
    if (!xs) return;

    // Спека: первый configure отправляется в ответ на commit без буфера.
    if (!xs->initial_configure_sent) {
        if (s.has_content)
            std::fprintf(stderr, "imway: клиент прикрепил буфер до configure (нарушение спеки)\n");
        send_configure(*xs);
        return;
    }

    if (xs->toplevel && !xs->toplevel->mapped && s.has_content && xs->acked) {
        xs->toplevel->mapped = true;
        s.server->needs_frame = true;
        std::printf("imway: toplevel «%s» (%s) mapped %dx%d\n", xs->toplevel->title.c_str(),
                    xs->toplevel->app_id.c_str(), s.width, s.height);
        if (s.server->seat) s.server->seat->focus_toplevel(xs->toplevel); // focus-on-map
    }
    if (xs->toplevel && xs->toplevel->mapped && !s.has_content) {
        xs->toplevel->mapped = false;
        s.server->needs_frame = true;
        std::printf("imway: toplevel «%s» unmapped\n", xs->toplevel->title.c_str());
    }

    if (xs->popup && !xs->popup->mapped && s.has_content && xs->acked) {
        xs->popup->mapped = true;
        s.server->needs_frame = true;
        std::printf("imway: popup mapped %dx%d @ (%d,%d)%s\n", s.width, s.height, xs->popup->x,
                    xs->popup->y, xs->popup->grab ? " grab" : "");
        if (xs->popup->grab && s.server->seat) s.server->seat->popup_grab_start(xs->popup);
    }
    if (xs->popup && xs->popup->mapped && !s.has_content) {
        xs->popup->mapped = false;
        s.server->needs_frame = true;
    }
}

void xdg_popup_dismiss(Popup& p) {
    if (!p.mapped) return;
    p.mapped = false;
    p.server->needs_frame = true;
    if (p.server->seat) p.server->seat->popup_gone(&p);
    xdg_popup_send_popup_done(p.res);
}

void xdg_shell_create_global(Server& server) {
    // v3: repositioned-событие у попапов
    wl_global_create(server.display, &xdg_wm_base_interface, 3, &server, wm_base_bind);
}
