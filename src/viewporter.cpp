// wp_viewporter: кроп и масштабирование поверхностей.

#include <viewporter-server-protocol.h>
#include <wayland-server-protocol.h>

#include "server.hpp"

namespace {

Surface* surface_of(wl_resource* res) { return (Surface*)wl_resource_get_user_data(res); }

// --- wp_viewport ---

void viewport_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void viewport_set_source(wl_client*, wl_resource* res, wl_fixed_t x, wl_fixed_t y, wl_fixed_t w,
                         wl_fixed_t h) {
    Surface* s = surface_of(res);
    if (!s) {
        wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "поверхность уничтожена");
        return;
    }
    double dx = wl_fixed_to_double(x), dy = wl_fixed_to_double(y);
    double dw = wl_fixed_to_double(w), dh = wl_fixed_to_double(h);
    if (dx == -1 && dy == -1 && dw == -1 && dh == -1) { // unset
        s->vp.pend_sw = s->vp.pend_sh = -1;
        return;
    }
    if (dx < 0 || dy < 0 || dw <= 0 || dh <= 0) {
        wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "некорректный source rect");
        return;
    }
    s->vp.pend_sx = dx;
    s->vp.pend_sy = dy;
    s->vp.pend_sw = dw;
    s->vp.pend_sh = dh;
}

void viewport_set_destination(wl_client*, wl_resource* res, int32_t w, int32_t h) {
    Surface* s = surface_of(res);
    if (!s) {
        wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "поверхность уничтожена");
        return;
    }
    if (w == -1 && h == -1) { // unset
        s->vp.pend_dw = s->vp.pend_dh = -1;
        return;
    }
    if (w <= 0 || h <= 0) {
        wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "некорректный destination");
        return;
    }
    s->vp.pend_dw = w;
    s->vp.pend_dh = h;
}

const struct wp_viewport_interface viewport_impl = {
    .destroy = viewport_destroy,
    .set_source = viewport_set_source,
    .set_destination = viewport_set_destination,
};

void viewport_resource_destroyed(wl_resource* res) {
    Surface* s = surface_of(res);
    if (!s) return;
    // спека: состояние снимается на следующем commit
    s->vp.res = nullptr;
    s->vp.pend_sw = s->vp.pend_sh = -1;
    s->vp.pend_dw = s->vp.pend_dh = -1;
}

// --- wp_viewporter ---

void viewporter_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void viewporter_get_viewport(wl_client* client, wl_resource* res, uint32_t id,
                             wl_resource* surface_res) {
    Surface* s = (Surface*)wl_resource_get_user_data(surface_res);
    if (s->vp.res) {
        wl_resource_post_error(res, WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS,
                               "у поверхности уже есть вьюпорт");
        return;
    }
    wl_resource* vres =
        wl_resource_create(client, &wp_viewport_interface, wl_resource_get_version(res), id);
    if (!vres) {
        wl_client_post_no_memory(client);
        return;
    }
    s->vp.res = vres;
    wl_resource_set_implementation(vres, &viewport_impl, s, viewport_resource_destroyed);
}

const struct wp_viewporter_interface viewporter_impl = {
    .destroy = viewporter_destroy,
    .get_viewport = viewporter_get_viewport,
};

void viewporter_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    wl_resource* res = wl_resource_create(client, &wp_viewporter_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &viewporter_impl, data, nullptr);
}

} // namespace

void viewport_apply_pending(Surface& s) {
    s.vp.has_src = s.vp.pend_sw > 0;
    if (s.vp.has_src) {
        s.vp.sx = s.vp.pend_sx;
        s.vp.sy = s.vp.pend_sy;
        s.vp.sw = s.vp.pend_sw;
        s.vp.sh = s.vp.pend_sh;
    }
    s.vp.has_dst = s.vp.pend_dw > 0;
    if (s.vp.has_dst) {
        s.vp.dw = s.vp.pend_dw;
        s.vp.dh = s.vp.pend_dh;
    }
}

void viewporter_create_global(Server& server) {
    wl_global_create(server.display, &wp_viewporter_interface, 1, &server, viewporter_bind);
}

// при уничтожении поверхности вьюпорт становится инертным
void viewport_surface_gone(Surface& s) {
    if (s.vp.res) wl_resource_set_user_data(s.vp.res, nullptr);
}
