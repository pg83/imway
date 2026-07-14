// xdg_wm_base / xdg_surface / xdg_toplevel / xdg_popup / xdg_positioner.

#include "renderer.h"
#include "seat.h"
#include "server.h"
#include "util.h"

#include <string.h>

#include <wayland-server-protocol.h>
#include <xdg-shell-server-protocol.h>

#include <std/ios/sys.h>

using namespace stl;

namespace {
    void copyBounded(char* dst, size_t cap, const char* src) {
        size_t len = strlen(src);

        if (len >= cap) {
            len = cap - 1;
        }

        memcpy(dst, src, len);
        dst[len] = 0;
    }

    // --- xdg_toplevel ---

    void toplevelDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void toplevelSetParent(wl_client*, wl_resource*, wl_resource*) {
    }

    void toplevelSetTitle(wl_client*, wl_resource* res, const char* title) {
        auto* t = (Toplevel*)wl_resource_get_user_data(res);

        copyBounded(t->title, sizeof(t->title), title);
    }

    void toplevelSetAppId(wl_client*, wl_resource* res, const char* appId) {
        auto* t = (Toplevel*)wl_resource_get_user_data(res);

        copyBounded(t->appId, sizeof(t->appId), appId);
    }

    void toplevelShowWindowMenu(wl_client*, wl_resource*, wl_resource*, u32, i32, i32) {
    }

    void toplevelMove(wl_client*, wl_resource*, wl_resource*, u32) {
    }

    void toplevelResize(wl_client*, wl_resource*, wl_resource*, u32, u32) {
    }

    void toplevelSetMaxSize(wl_client*, wl_resource*, i32, i32) {
    }

    void toplevelSetMinSize(wl_client*, wl_resource*, i32, i32) {
    }

    void toplevelSetMaximized(wl_client*, wl_resource*) {
    }

    void toplevelUnsetMaximized(wl_client*, wl_resource*) {
    }

    void toplevelSetFullscreen(wl_client*, wl_resource*, wl_resource*) {
    }

    void toplevelUnsetFullscreen(wl_client*, wl_resource*) {
    }

    void toplevelSetMinimized(wl_client*, wl_resource*) {
    }

    const struct xdg_toplevel_interface toplevelImpl = {
        .destroy = toplevelDestroy,
        .set_parent = toplevelSetParent,
        .set_title = toplevelSetTitle,
        .set_app_id = toplevelSetAppId,
        .show_window_menu = toplevelShowWindowMenu,
        .move = toplevelMove,
        .resize = toplevelResize,
        .set_max_size = toplevelSetMaxSize,
        .set_min_size = toplevelSetMinSize,
        .set_maximized = toplevelSetMaximized,
        .unset_maximized = toplevelUnsetMaximized,
        .set_fullscreen = toplevelSetFullscreen,
        .unset_fullscreen = toplevelUnsetFullscreen,
        .set_minimized = toplevelSetMinimized,
    };

    void toplevelResourceDestroyed(wl_resource* res) {
        auto* t = (Toplevel*)wl_resource_get_user_data(res);
        Server* server = t->server;

        if (server->seat) {
            server->seat->toplevelGone(t);
        }

        if (t->xdg) {
            t->xdg->toplevel = nullptr;
        }

        removeOne(server->toplevels, t);
        sysO << "imway: toplevel "_sv << (const char*)t->title << " destroyed"_sv << endL;
        server->needsFrame = true;
        server->toplevelAlloc->release(t);
    }

    // --- xdg_surface ---

    void xdgSurfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void sendConfigure(XdgSurface& xs) {
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
        xs.initialConfigureSent = true;
    }

    void xdgSurfaceGetToplevel(wl_client* client, wl_resource* res, u32 id) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);
        wl_resource* tres =
            wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(res), id);

        if (!tres) {
            wl_client_post_no_memory(client);

            return;
        }

        Server* server = xs->server;
        auto* t = server->toplevelAlloc->make();

        t->server = server;
        t->res = tres;
        t->xdg = xs;
        t->id = server->nextToplevelId++;
        xs->toplevel = t;
        server->toplevels.pushBack(t);
        wl_resource_set_implementation(tres, &toplevelImpl, t, toplevelResourceDestroyed);
    }

    // определение ниже: нужны Positioner и popupImpl из секции попапов
    void xdgSurfaceGetPopup(wl_client* client, wl_resource* res, u32 id, wl_resource* parentRes,
                            wl_resource* positionerRes);

    void xdgSurfaceSetWindowGeometry(wl_client*, wl_resource*, i32, i32, i32, i32) {
        // рисуем буфер целиком; кроп по geometry не делаем
    }

    void xdgSurfaceAckConfigure(wl_client*, wl_resource* res, u32) {
        ((XdgSurface*)wl_resource_get_user_data(res))->acked = true;
    }

    const struct xdg_surface_interface xdgSurfaceImpl = {
        .destroy = xdgSurfaceDestroy,
        .get_toplevel = xdgSurfaceGetToplevel,
        .get_popup = xdgSurfaceGetPopup,
        .set_window_geometry = xdgSurfaceSetWindowGeometry,
        .ack_configure = xdgSurfaceAckConfigure,
    };

    void xdgSurfaceResourceDestroyed(wl_resource* res) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);

        if (xs->surface) {
            xs->surface->xdg = nullptr;
        }

        if (xs->toplevel) {
            xs->toplevel->xdg = nullptr;
        }

        if (xs->popup) {
            xs->popup->xdg = nullptr;
        }

        xs->server->xdgSurfaceAlloc->release(xs);
    }

    // --- xdg_positioner ---

    struct XdgShellState;

    struct Positioner {
        int w = 0, h = 0;                   // set_size
        int ax = 0, ay = 0, aw = 0, ah = 0; // anchor_rect
        u32 anchor = XDG_POSITIONER_ANCHOR_NONE;
        u32 gravity = XDG_POSITIONER_GRAVITY_NONE;
        int dx = 0, dy = 0; // offset

        XdgShellState* state = nullptr; // для возврата в ObjList при destroy

        // левый-верхний угол попапа в координатах родителя
        void place(int& outX, int& outY) const;
    };

    struct XdgShellState { // user data глобала xdg_wm_base
        Server* server = nullptr;
        ObjList<Positioner>* positioners = nullptr;
    };

    Positioner* positionerFrom(wl_resource* res) {
        return (Positioner*)wl_resource_get_user_data(res);
    }

    void positionerDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void positionerSetSize(wl_client*, wl_resource* res, i32 w, i32 h) {
        Positioner* p = positionerFrom(res);

        p->w = w;
        p->h = h;
    }

    void positionerSetAnchorRect(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        Positioner* p = positionerFrom(res);

        p->ax = x;
        p->ay = y;
        p->aw = w;
        p->ah = h;
    }

    void positionerSetAnchor(wl_client*, wl_resource* res, u32 a) {
        positionerFrom(res)->anchor = a;
    }

    void positionerSetGravity(wl_client*, wl_resource* res, u32 g) {
        positionerFrom(res)->gravity = g;
    }

    void positionerSetConstraintAdjustment(wl_client*, wl_resource*, u32) {
        // слайды/флипы у краёв — позже; в ImGui-окне попап и так виден
    }

    void positionerSetOffset(wl_client*, wl_resource* res, i32 x, i32 y) {
        Positioner* p = positionerFrom(res);

        p->dx = x;
        p->dy = y;
    }

    void positionerSetReactive(wl_client*, wl_resource*) {
    }

    void positionerSetParentSize(wl_client*, wl_resource*, i32, i32) {
    }

    void positionerSetParentConfigure(wl_client*, wl_resource*, u32) {
    }

    const struct xdg_positioner_interface positionerImpl = {
        .destroy = positionerDestroy,
        .set_size = positionerSetSize,
        .set_anchor_rect = positionerSetAnchorRect,
        .set_anchor = positionerSetAnchor,
        .set_gravity = positionerSetGravity,
        .set_constraint_adjustment = positionerSetConstraintAdjustment,
        .set_offset = positionerSetOffset,
        .set_reactive = positionerSetReactive,
        .set_parent_size = positionerSetParentSize,
        .set_parent_configure = positionerSetParentConfigure,
    };

    void positionerResourceDestroyed(wl_resource* res) {
        Positioner* p = positionerFrom(res);

        p->state->positioners->release(p);
    }

    // --- xdg_popup ---

    void popupDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void popupGrab(wl_client*, wl_resource* res, wl_resource* /*seat*/, u32 /*serial*/) {
        auto* p = (Popup*)wl_resource_get_user_data(res);

        p->grab = true;
    }

    void popupReposition(wl_client*, wl_resource* res, wl_resource* positioner, u32 token) {
        auto* p = (Popup*)wl_resource_get_user_data(res);
        Positioner* pos = positionerFrom(positioner);

        pos->place(p->x, p->y);
        p->w = pos->w;
        p->h = pos->h;
        xdg_popup_send_repositioned(res, token);
        xdg_popup_send_configure(res, p->x, p->y, p->w, p->h);
        xdg_surface_send_configure(p->xdg->res, wl_display_next_serial(p->server->display));
        p->server->needsFrame = true;
    }

    const struct xdg_popup_interface popupImpl = {
        .destroy = popupDestroy,
        .grab = popupGrab,
        .reposition = popupReposition,
    };

    void popupResourceDestroyed(wl_resource* res) {
        auto* p = (Popup*)wl_resource_get_user_data(res);
        Server* server = p->server;

        if (server->seat) {
            server->seat->popupGone(p);
        }

        if (p->xdg) {
            p->xdg->popup = nullptr;
        }

        removeOne(server->popups, p);
        server->needsFrame = true;
        server->popupAlloc->release(p);
    }

    void xdgSurfaceGetPopup(wl_client* client, wl_resource* res, u32 id, wl_resource* parentRes,
                            wl_resource* positionerRes) {
        auto* xs = (XdgSurface*)wl_resource_get_user_data(res);

        if (!parentRes) {
            wl_resource_post_error(res, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                                   "попап без родителя не поддержан");

            return;
        }

        auto* parentXs = (XdgSurface*)wl_resource_get_user_data(parentRes);
        wl_resource* pres =
            wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        Server* server = xs->server;
        auto* p = server->popupAlloc->make();

        p->server = server;
        p->res = pres;
        p->xdg = xs;
        p->parent = parentXs->surface;

        Positioner* pos = positionerFrom(positionerRes);

        pos->place(p->x, p->y);
        p->w = pos->w;
        p->h = pos->h;
        xs->popup = p;
        server->popups.pushBack(p);
        wl_resource_set_implementation(pres, &popupImpl, p, popupResourceDestroyed);
    }

    // --- xdg_wm_base ---

    void wmBaseDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void wmBaseCreatePositioner(wl_client* client, wl_resource* res, u32 id) {
        auto* state = (XdgShellState*)wl_resource_get_user_data(res);
        wl_resource* pres =
            wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        Positioner* p = state->positioners->make();

        p->state = state;
        wl_resource_set_implementation(pres, &positionerImpl, p, positionerResourceDestroyed);
    }

    void wmBaseGetXdgSurface(wl_client* client, wl_resource* res, u32 id,
                             wl_resource* surfaceRes) {
        auto* state = (XdgShellState*)wl_resource_get_user_data(res);
        auto* surface = (Surface*)wl_resource_get_user_data(surfaceRes);
        wl_resource* xres =
            wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(res), id);

        if (!xres) {
            wl_client_post_no_memory(client);

            return;
        }

        Server* server = state->server;
        auto* xs = server->xdgSurfaceAlloc->make();

        xs->server = server;
        xs->res = xres;
        xs->surface = surface;
        surface->xdg = xs;
        wl_resource_set_implementation(xres, &xdgSurfaceImpl, xs, xdgSurfaceResourceDestroyed);
    }

    void wmBasePong(wl_client*, wl_resource*, u32) {
    }

    const struct xdg_wm_base_interface wmBaseImpl = {
        .destroy = wmBaseDestroy,
        .create_positioner = wmBaseCreatePositioner,
        .get_xdg_surface = wmBaseGetXdgSurface,
        .pong = wmBasePong,
    };

    void wmBaseBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &xdg_wm_base_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &wmBaseImpl, data, nullptr);
    }
}

void Positioner::place(int& outX, int& outY) const {
    int px = ax, py = ay; // якорная точка на anchor_rect

    switch (anchor) {
        case XDG_POSITIONER_ANCHOR_TOP:
            px += aw / 2;
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM:
            px += aw / 2;
            py += ah;
            break;
        case XDG_POSITIONER_ANCHOR_LEFT:
            py += ah / 2;
            break;
        case XDG_POSITIONER_ANCHOR_RIGHT:
            px += aw;
            py += ah / 2;
            break;
        case XDG_POSITIONER_ANCHOR_TOP_LEFT:
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
            py += ah;
            break;
        case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
            px += aw;
            break;
        case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
            px += aw;
            py += ah;
            break;
        default: // NONE = центр
            px += aw / 2;
            py += ah / 2;
            break;
    }

    // gravity: в какую сторону попап растёт от якоря
    switch (gravity) {
        case XDG_POSITIONER_GRAVITY_TOP:
            px -= w / 2;
            py -= h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM:
            px -= w / 2;
            break;
        case XDG_POSITIONER_GRAVITY_LEFT:
            px -= w;
            py -= h / 2;
            break;
        case XDG_POSITIONER_GRAVITY_RIGHT:
            py -= h / 2;
            break;
        case XDG_POSITIONER_GRAVITY_TOP_LEFT:
            px -= w;
            py -= h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
            px -= w;
            break;
        case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
            py -= h;
            break;
        case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
            break;
        default: // NONE = центр
            px -= w / 2;
            py -= h / 2;
            break;
    }

    outX = px + dx;
    outY = py + dy;
}

void xdgToplevelConfigureSize(Toplevel& t, int w, int h) {
    wl_array states;

    wl_array_init(&states);
    xdg_toplevel_send_configure(t.res, w, h, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(t.xdg->res, wl_display_next_serial(t.server->display));
    t.cfgW = w;
    t.cfgH = h;
    sysO << "imway: configure "_sv << (const char*)t.title << " -> "_sv << w << "x"_sv << h << endL;
}

void xdgHandleCommit(Surface& s) {
    XdgSurface* xs = s.xdg;

    if (!xs) {
        return;
    }

    // Спека: первый configure отправляется в ответ на commit без буфера.
    if (!xs->initialConfigureSent) {
        if (s.hasContent) {
            sysE << "imway: client attached a buffer before configure (spec violation)"_sv << endL;
        }

        sendConfigure(*xs);

        return;
    }

    if (xs->toplevel && !xs->toplevel->mapped && s.hasContent && xs->acked) {
        xs->toplevel->mapped = true;
        s.server->needsFrame = true;
        sysO << "imway: toplevel "_sv << (const char*)xs->toplevel->title << " ("_sv << (const char*)xs->toplevel->appId
             << ") mapped "_sv << s.width << "x"_sv << s.height << endL;

        if (s.server->seat) {
            s.server->seat->focusToplevel(xs->toplevel); // focus-on-map
        }
    }

    if (xs->toplevel && xs->toplevel->mapped && !s.hasContent) {
        xs->toplevel->mapped = false;
        s.server->needsFrame = true;
        sysO << "imway: toplevel "_sv << (const char*)xs->toplevel->title << " unmapped"_sv << endL;
    }

    if (xs->popup && !xs->popup->mapped && s.hasContent && xs->acked) {
        xs->popup->mapped = true;
        s.server->needsFrame = true;
        sysO << "imway: popup mapped "_sv << s.width << "x"_sv << s.height << " at ("_sv << xs->popup->x
             << ","_sv << xs->popup->y << ")"_sv << (xs->popup->grab ? " grab" : "") << endL;

        if (xs->popup->grab && s.server->seat) {
            s.server->seat->popupGrabStart(xs->popup);
        }
    }

    if (xs->popup && xs->popup->mapped && !s.hasContent) {
        xs->popup->mapped = false;
        s.server->needsFrame = true;
    }
}

void xdgPopupDismiss(Popup& p) {
    if (!p.mapped) {
        return;
    }

    p.mapped = false;
    p.server->needsFrame = true;

    if (p.server->seat) {
        p.server->seat->popupGone(&p);
    }

    xdg_popup_send_popup_done(p.res);
}

void xdgShellCreateGlobal(Server& server) {
    auto* state = server.pool->make<XdgShellState>();

    state->server = &server;
    state->positioners = server.pool->make<ObjList<Positioner>>(server.pool);

    // v3: repositioned-событие у попапов
    wl_global_create(server.display, &xdg_wm_base_interface, 3, state, wmBaseBind);
}
