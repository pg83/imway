// wp_viewporter: кроп и масштабирование поверхностей.

#include "server.h"

#include <viewporter-server-protocol.h>
#include <wayland-server-protocol.h>

namespace {
    Surface* surfaceOf(wl_resource* res) {
        return (Surface*)wl_resource_get_user_data(res);
    }

    // --- wp_viewport ---

    void viewportDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void viewportSetSource(wl_client*, wl_resource* res, wl_fixed_t x, wl_fixed_t y, wl_fixed_t w,
                           wl_fixed_t h) {
        Surface* s = surfaceOf(res);

        if (!s) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "поверхность уничтожена");

            return;
        }

        double dx = wl_fixed_to_double(x), dy = wl_fixed_to_double(y);
        double dw = wl_fixed_to_double(w), dh = wl_fixed_to_double(h);

        if (dx == -1 && dy == -1 && dw == -1 && dh == -1) { // unset
            s->vp.pendSw = s->vp.pendSh = -1;

            return;
        }

        if (dx < 0 || dy < 0 || dw <= 0 || dh <= 0) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "некорректный source rect");

            return;
        }

        s->vp.pendSx = dx;
        s->vp.pendSy = dy;
        s->vp.pendSw = dw;
        s->vp.pendSh = dh;
    }

    void viewportSetDestination(wl_client*, wl_resource* res, i32 w, i32 h) {
        Surface* s = surfaceOf(res);

        if (!s) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "поверхность уничтожена");

            return;
        }

        if (w == -1 && h == -1) { // unset
            s->vp.pendDw = s->vp.pendDh = -1;

            return;
        }

        if (w <= 0 || h <= 0) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "некорректный destination");

            return;
        }

        s->vp.pendDw = w;
        s->vp.pendDh = h;
    }

    const struct wp_viewport_interface viewportImpl = {
        .destroy = viewportDestroy,
        .set_source = viewportSetSource,
        .set_destination = viewportSetDestination,
    };

    void viewportResourceDestroyed(wl_resource* res) {
        Surface* s = surfaceOf(res);

        if (!s) {
            return;
        }

        // спека: состояние снимается на следующем commit
        s->vp.res = nullptr;
        s->vp.pendSw = s->vp.pendSh = -1;
        s->vp.pendDw = s->vp.pendDh = -1;
    }

    // --- wp_viewporter ---

    void viewporterDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void viewporterGetViewport(wl_client* client, wl_resource* res, u32 id,
                               wl_resource* surfaceRes) {
        Surface* s = (Surface*)wl_resource_get_user_data(surfaceRes);

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
        wl_resource_set_implementation(vres, &viewportImpl, s, viewportResourceDestroyed);
    }

    const struct wp_viewporter_interface viewporterImpl = {
        .destroy = viewporterDestroy,
        .get_viewport = viewporterGetViewport,
    };

    void viewporterBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wp_viewporter_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &viewporterImpl, data, nullptr);
    }
}

void viewportApplyPending(Surface& s) {
    s.vp.hasSrc = s.vp.pendSw > 0;

    if (s.vp.hasSrc) {
        s.vp.sx = s.vp.pendSx;
        s.vp.sy = s.vp.pendSy;
        s.vp.sw = s.vp.pendSw;
        s.vp.sh = s.vp.pendSh;
    }

    s.vp.hasDst = s.vp.pendDw > 0;

    if (s.vp.hasDst) {
        s.vp.dw = s.vp.pendDw;
        s.vp.dh = s.vp.pendDh;
    }
}

// при уничтожении поверхности вьюпорт становится инертным
void viewportSurfaceGone(Surface& s) {
    if (s.vp.res) {
        wl_resource_set_user_data(s.vp.res, nullptr);
    }
}

void viewporterCreateGlobal(Server& server) {
    wl_global_create(server.display, &wp_viewporter_interface, 1, &server, viewporterBind);
}
