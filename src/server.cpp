// Ядро композитора: event loop, все wayland-глобалы и обработчики протоколов.

#include "server.h"

#include "control.h"
#include "input_linux.h"
#include "kms.h"
#include "renderer.h"
#include "seat.h"
#include "util.h"

#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ev.h>
#include <linux-dmabuf-v1-server-protocol.h>
#include <viewporter-server-protocol.h>
#include <wayland-server-protocol.h>
#include <xdg-decoration-unstable-v1-server-protocol.h>
#include <xdg-shell-server-protocol.h>

#include <imgui.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    struct ServerImpl;

    // мелкие протокольные объекты с O(1) reuse; srv — для возврата в свой ObjList
    struct RegionBox {
        ServerImpl* srv = nullptr;
        Vector<RectI> rects;
    };

    struct Positioner {
        ServerImpl* srv = nullptr;

        int w = 0, h = 0;                   // set_size
        int ax = 0, ay = 0, aw = 0, ah = 0; // anchor_rect
        u32 anchor = XDG_POSITIONER_ANCHOR_NONE;
        u32 gravity = XDG_POSITIONER_GRAVITY_NONE;
        int dx = 0, dy = 0; // offset

        // левый-верхний угол попапа в координатах родителя
        void place(int& outX, int& outY) const;
    };

    struct BufferBox { // dmabuf wl_buffer
        ServerImpl* srv = nullptr;
        DmabufBuffer buf;
    };

    struct Params { // zwp_linux_buffer_params_v1
        ServerImpl* srv = nullptr;
        BufferBox* pending = nullptr; // накапливаем add(); nullptr после create
    };

    struct ServerImpl: public Server {
        ObjPool* pool = nullptr;
        ServerConfig cfg;

        wl_event_loop* wlLoop = nullptr;

        Kms* kms = nullptr;
        InputLinux* input = nullptr;
        Control* control = nullptr;

        ev_io wlIo{};
        ev_prepare flushPrepare{};
        ev_timer frameTimer{};
        ev_signal sigInt{}, sigTerm{};
        bool watchersStarted = false;

        u64 nextToplevelId = 1;
        int settleFrames = 0; // дорисовать пару кадров после последней активности

        // переиспользуемые аллокации протокольных объектов (память из пула)
        ObjList<Surface>* surfaceAlloc = nullptr;
        ObjList<Subsurface>* subsurfaceAlloc = nullptr;
        ObjList<XdgSurface>* xdgSurfaceAlloc = nullptr;
        ObjList<Toplevel>* toplevelAlloc = nullptr;
        ObjList<Popup>* popupAlloc = nullptr;
        ObjList<RegionBox>* regionAlloc = nullptr;
        ObjList<Positioner>* positionerAlloc = nullptr;
        ObjList<BufferBox>* dmabufBoxAlloc = nullptr;
        ObjList<Params>* dmabufParamsAlloc = nullptr;

        ServerImpl(ObjPool* p, const ServerConfig& config);
        ~ServerImpl() noexcept override;

        void run() override;
        void dismissPopup(Popup& p) override;

        void createGlobals();
        void onFrameTick();
    };

    ServerImpl& impl(Server* s) {
        return *(ServerImpl*)s;
    }

    // --- event loop ---

    void wlIoCb(struct ev_loop*, ev_io* w, int) {
        auto* s = (ServerImpl*)w->data;

        wl_event_loop_dispatch(s->wlLoop, 0);
    }

    // Инвариант libwayland: не засыпать с несброшенными буферами клиентов.
    void flushCb(struct ev_loop*, ev_prepare* w, int) {
        auto* s = (ServerImpl*)w->data;

        wl_display_flush_clients(s->display);
    }

    void frameCb(struct ev_loop*, ev_timer* w, int) {
        ((ServerImpl*)w->data)->onFrameTick();
    }

    void signalCb(struct ev_loop* loop, ev_signal*, int) {
        ev_break(loop, EVBREAK_ALL);
    }

    void fireFrameCallbacks(Surface& s, u32 t) {
        // деструктор ресурса удаляет callback из frameCbs — забираем список до итерации
        Vector<wl_resource*> cbs;

        cbs.xchg(s.frameCbs);

        for (wl_resource* cb : cbs) {
            wl_callback_send_done(cb, t);
            wl_resource_destroy(cb);
        }

        for (Subsurface* c : s.stackBelow) {
            if (c->surface) {
                fireFrameCallbacks(*c->surface, t);
            }
        }

        for (Subsurface* c : s.stackAbove) {
            if (c->surface) {
                fireFrameCallbacks(*c->surface, t);
            }
        }
    }

    void copyBounded(char* dst, size_t cap, const char* src) {
        size_t len = strlen(src);

        if (len >= cap) {
            len = cap - 1;
        }

        memcpy(dst, src, len);
        dst[len] = 0;
    }

    void resDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    // объявления для перекрёстных ссылок между секциями
    void unlinkFromParent(Subsurface&);
    void applySubsurfaceCache(Subsurface&);
    void xdgHandleCommit(Surface&);
    void xdgPopupDismiss(Popup&);
    void viewportApplyPending(Surface&);
    void viewportSurfaceGone(Surface&);

    // ================= wl_surface / wl_region / wl_subcompositor =================

    Surface* surfaceFrom(wl_resource* res) {
        return (Surface*)wl_resource_get_user_data(res);
    }

    void detachPendingBuffer(Surface& s) {
        if (s.pending.bufferDestroyArmed) {
            wl_list_remove(&s.pending.bufferDestroy.link);
            s.pending.bufferDestroyArmed = false;
        }

        s.pending.buffer = nullptr;
    }

    void pendingBufferDestroyed(wl_listener* l, void*) {
        Surface* s = wl_container_of(l, s, pending.bufferDestroy);

        s->pending.buffer = nullptr;
        s->pending.bufferDestroyArmed = false;
        wl_list_remove(&s->pending.bufferDestroy.link);
    }

    // --- удержание dmabuf-буфера (рендер читает его память напрямую) ---

    void heldDmabufDestroyed(wl_listener* l, void*) {
        Surface* s = wl_container_of(l, s, dmabufDestroy);

        // клиент уничтожил буфер, пока тот показан: текстура уже импортирована
        // (память живёт на нашем fd-дубликате), просто забываем ресурс
        s->dmabufBuffer = nullptr;
        s->dmabufDestroyArmed = false;
        wl_list_remove(&s->dmabufDestroy.link);
    }

    void releaseHeldDmabuf(Surface& s) {
        if (!s.dmabufBuffer) {
            return;
        }

        wl_buffer_send_release(s.dmabufBuffer);

        if (s.dmabufDestroyArmed) {
            wl_list_remove(&s.dmabufDestroy.link);
            s.dmabufDestroyArmed = false;
        }

        s.dmabufBuffer = nullptr;
    }

    void holdDmabuf(Surface& s, wl_resource* buffer) {
        releaseHeldDmabuf(s);

        s.dmabufBuffer = buffer;
        s.dmabufDestroy.notify = heldDmabufDestroyed;
        wl_resource_add_destroy_listener(buffer, &s.dmabufDestroy);
        s.dmabufDestroyArmed = true;
    }

    void surfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void surfaceAttach(wl_client*, wl_resource* res, wl_resource* buffer, i32, i32) {
        Surface& s = *surfaceFrom(res);

        detachPendingBuffer(s);
        s.pending.buffer = buffer;
        s.pending.newlyAttached = true;

        if (buffer) {
            s.pending.bufferDestroy.notify = pendingBufferDestroyed;
            wl_resource_add_destroy_listener(buffer, &s.pending.bufferDestroy);
            s.pending.bufferDestroyArmed = true;
        }
    }

    void surfaceDamage(wl_client*, wl_resource*, i32, i32, i32, i32) {
        // полная перезаливка на каждый commit, damage-rects учтём позже
    }

    void frameCallbackDestroyed(wl_resource* cb) {
        Surface* s = (Surface*)wl_resource_get_user_data(cb);

        if (!s) {
            return;
        }

        removeOne(s->pending.frames, cb);
        removeOne(s->frameCbs, cb);
    }

    void surfaceFrame(wl_client* client, wl_resource* res, u32 id) {
        Surface& s = *surfaceFrom(res);
        wl_resource* cb = wl_resource_create(client, &wl_callback_interface, 1, id);

        if (!cb) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(cb, nullptr, &s, frameCallbackDestroyed);
        s.pending.frames.pushBack(cb);
    }

    void surfaceSetOpaqueRegion(wl_client*, wl_resource*, wl_resource*) {
    }

    void surfaceSetInputRegion(wl_client*, wl_resource* res, wl_resource* region) {
        Surface& s = *surfaceFrom(res);

        if (!region) { // NULL = вся поверхность
            s.pending.inputRegionSet = false;
            s.pending.inputRegion.clear();

            return;
        }

        // регион копируется в момент запроса (клиент может сразу уничтожить его)
        auto* box = (RegionBox*)wl_resource_get_user_data(region);

        s.pending.inputRegionSet = true;
        s.pending.inputRegion.clear();
        s.pending.inputRegion.append(box->rects.begin(), box->rects.length());
    }

    void copyShmBufferTo(wl_shm_buffer& shm, int& outW, int& outH, Vector<u8>& out) {
        i32 w = wl_shm_buffer_get_width(&shm);
        i32 h = wl_shm_buffer_get_height(&shm);
        i32 stride = wl_shm_buffer_get_stride(&shm);
        u32 fmt = wl_shm_buffer_get_format(&shm);

        if (fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XRGB8888) {
            sysE << "imway: unsupported shm format "_sv << fmt << endL;
            outW = outH = 0;

            return;
        }

        outW = w;
        outH = h;
        out.clear();
        out.grow((size_t)w * h * 4);

        wl_shm_buffer_begin_access(&shm);

        auto* src = (const u8*)wl_shm_buffer_get_data(&shm);

        for (i32 y = 0; y < h; y++) {
            out.append(src + (size_t)y * stride, (size_t)w * 4);
        }

        wl_shm_buffer_end_access(&shm);
    }

    void copyShmBuffer(Surface& s, wl_shm_buffer* shm) {
        copyShmBufferTo(*shm, s.width, s.height, s.pixels);

        if (s.width > 0) {
            s.dirty = true;
            s.hasContent = true;
        }
    }

    void applyChildrenCaches(Surface& s) {
        Vector<Subsurface*>* stacks[] = {&s.stackBelow, &s.stackAbove};

        for (auto* stack : stacks) {
            for (Subsurface* c : *stack) {
                // позиция двойнобуферизована коммитом родителя для любых детей
                if (c->pendingPos) {
                    c->x = c->pendingX;
                    c->y = c->pendingY;
                    c->pendingPos = false;
                }

                if (c->sync) {
                    applySubsurfaceCache(*c);
                }
            }
        }
    }

    // применить кэш sync-субповерхности и рекурсивно кэши её sync-детей
    void applySubsurfaceCache(Subsurface& sub) {
        if (sub.cache.valid) {
            Surface& s = *sub.surface;

            s.hasContent = sub.cache.hasContent;
            s.width = sub.cache.width;
            s.height = sub.cache.height;

            if (sub.cache.hasContent && !sub.cache.pixels.empty()) {
                s.pixels.xchg(sub.cache.pixels);
                s.dirty = true;
            }

            for (wl_resource* cb : sub.cache.frames) {
                s.frameCbs.pushBack(cb);
            }

            sub.cache.frames.clear();
            sub.cache.pixels.clear();
            sub.cache.valid = false;
        }

        if (sub.pendingPos) {
            sub.x = sub.pendingX;
            sub.y = sub.pendingY;
            sub.pendingPos = false;
        }

        // спека: commit родителя применяет закешированное состояние всего sync-поддерева
        for (Subsurface* c : sub.surface->stackBelow) {
            if (c->sync) {
                applySubsurfaceCache(*c);
            }
        }

        for (Subsurface* c : sub.surface->stackAbove) {
            if (c->sync) {
                applySubsurfaceCache(*c);
            }
        }
    }

    void surfaceCommit(wl_client*, wl_resource* res) {
        Surface& s = *surfaceFrom(res);

        s.server->needsFrame = true;

        bool toCache = s.sub && s.sub->effectiveSync();

        if (s.pending.newlyAttached) {
            if (!s.pending.buffer) {
                if (toCache) {
                    s.sub->cache.valid = true;
                    s.sub->cache.hasContent = false;
                    s.sub->cache.width = s.sub->cache.height = 0;
                    s.sub->cache.pixels.clear();
                } else {
                    s.hasContent = false;
                    s.width = s.height = 0;
                }
            } else if (wl_shm_buffer* shm = wl_shm_buffer_get(s.pending.buffer)) {
                if (toCache) {
                    // снимаем копию сразу (буфер возвращается клиенту), показ — на commit родителя
                    copyShmBufferTo(*shm, s.sub->cache.width, s.sub->cache.height,
                                    s.sub->cache.pixels);
                    s.sub->cache.hasContent = s.sub->cache.width > 0;
                    s.sub->cache.valid = true;
                } else {
                    copyShmBuffer(s, shm);
                }

                wl_buffer_send_release(s.pending.buffer);
                releaseHeldDmabuf(s); // на случай смены dmabuf → shm
            } else if (DmabufBuffer* db = dmabufFromBufferResource(s.pending.buffer)) {
                // dmabuf применяем сразу даже для sync-субповерхностей (без кэша):
                // буфер один, копий нет — упрощение
                holdDmabuf(s, s.pending.buffer);
                s.width = db->width;
                s.height = db->height;
                s.pixels.clear();
                s.hasContent = true;
                s.dirty = true;
            } else {
                sysE << "imway: unknown buffer type"_sv << endL;
            }

            detachPendingBuffer(s);
            s.pending.newlyAttached = false;
        }

        // input region применяем сразу даже для sync-субповерхностей: хит-тест
        // не должен ждать commit родителя (упрощение, GTK-оверлеям достаточно)
        s.inputRegionSet = s.pending.inputRegionSet;
        s.inputRegion.clear();
        s.inputRegion.append(s.pending.inputRegion.begin(), s.pending.inputRegion.length());

        if (toCache) {
            for (wl_resource* cb : s.pending.frames) {
                s.sub->cache.frames.pushBack(cb);
            }

            s.pending.frames.clear();

            return; // остальное (кэши детей) — когда применится наш кэш
        }

        for (wl_resource* cb : s.pending.frames) {
            s.frameCbs.pushBack(cb);
        }

        s.pending.frames.clear();

        viewportApplyPending(s);

        // desync-субповерхность: позиция всё равно применяется коммитом родителя,
        // но контент — сразу (уже применён выше)
        applyChildrenCaches(s);

        if (s.xdg) {
            xdgHandleCommit(s);
        }
    }

    void surfaceSetBufferTransform(wl_client*, wl_resource*, i32) {
    }

    void surfaceSetBufferScale(wl_client*, wl_resource*, i32) {
    }

    void surfaceDamageBuffer(wl_client*, wl_resource*, i32, i32, i32, i32) {
    }

    void surfaceOffset(wl_client*, wl_resource*, i32, i32) {
    }

    const struct wl_surface_interface surfaceImpl = {
        .destroy = surfaceDestroy,
        .attach = surfaceAttach,
        .damage = surfaceDamage,
        .frame = surfaceFrame,
        .set_opaque_region = surfaceSetOpaqueRegion,
        .set_input_region = surfaceSetInputRegion,
        .commit = surfaceCommit,
        .set_buffer_transform = surfaceSetBufferTransform,
        .set_buffer_scale = surfaceSetBufferScale,
        .damage_buffer = surfaceDamageBuffer,
        .offset = surfaceOffset,
    };

    void surfaceResourceDestroyed(wl_resource* res) {
        Surface* s = surfaceFrom(res);
        Server* server = s->server;

        detachPendingBuffer(*s);

        for (wl_resource* cb : s->pending.frames) {
            wl_resource_set_user_data(cb, nullptr);
        }

        for (wl_resource* cb : s->frameCbs) {
            wl_resource_set_user_data(cb, nullptr);
        }

        if (s->xdg) {
            s->xdg->surface = nullptr;
        }

        if (s->sub) { // роль-субповерхность: выпасть из стека родителя
            unlinkFromParent(*s->sub);
            s->sub->surface = nullptr;
        }

        for (Subsurface* c : s->stackBelow) { // дети-сироты не рендерятся
            c->parent = nullptr;
        }

        for (Subsurface* c : s->stackAbove) {
            c->parent = nullptr;
        }

        for (Popup* p : server->popups) { // попапы умершего родителя гаснут
            if (p->parent == s) {
                p->parent = nullptr;

                if (p->mapped) {
                    xdgPopupDismiss(*p);
                }
            }
        }

        if (server->seat) {
            server->seat->surfaceGone(s);
        }

        releaseHeldDmabuf(*s);
        viewportSurfaceGone(*s);

        if (s->texture && server->renderer) {
            server->renderer->destroyTexture(s->texture);
        }

        server->needsFrame = true;
        removeOne(server->surfaces, s);
        impl(server).surfaceAlloc->release(s);
    }

    void regionDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void regionAdd(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        ((RegionBox*)wl_resource_get_user_data(res))->rects.pushBack({x, y, w, h});
    }

    void regionSubtract(wl_client*, wl_resource* res, i32 x, i32 y, i32 w, i32 h) {
        // честная булева геометрия не нужна для input-хит-теста наших клиентов;
        // выкидываем целиком совпавшие прямоугольники, частичные пересечения игнорируем
        auto& v = ((RegionBox*)wl_resource_get_user_data(res))->rects;
        size_t keep = 0;

        for (size_t i = 0; i < v.length(); i++) {
            const RectI& r = v[i];

            if (!(r.x >= x && r.y >= y && r.x + r.w <= x + w && r.y + r.h <= y + h)) {
                v.mut(keep++) = r;
            }
        }

        while (v.length() > keep) {
            v.popBack();
        }
    }

    const struct wl_region_interface regionImpl = {
        .destroy = regionDestroy,
        .add = regionAdd,
        .subtract = regionSubtract,
    };

    void regionResourceDestroyed(wl_resource* res) {
        auto* box = (RegionBox*)wl_resource_get_user_data(res);

        box->srv->regionAlloc->release(box);
    }

    void compositorCreateSurface(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (ServerImpl*)wl_resource_get_user_data(res);
        wl_resource* sres =
            wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(res), id);

        if (!sres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* s = srv->surfaceAlloc->make();

        s->server = srv;
        s->res = sres;
        srv->surfaces.pushBack(s);
        wl_resource_set_implementation(sres, &surfaceImpl, s, surfaceResourceDestroyed);
    }

    void compositorCreateRegion(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (ServerImpl*)wl_resource_get_user_data(res);
        wl_resource* rres =
            wl_resource_create(client, &wl_region_interface, wl_resource_get_version(res), id);

        if (!rres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* box = srv->regionAlloc->make();

        box->srv = srv;
        wl_resource_set_implementation(rres, &regionImpl, box, regionResourceDestroyed);
    }

    const struct wl_compositor_interface compositorImpl = {
        .create_surface = compositorCreateSurface,
        .create_region = compositorCreateRegion,
    };

    void compositorBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wl_compositor_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &compositorImpl, data, nullptr);
    }

    Subsurface* subFrom(wl_resource* res) {
        return (Subsurface*)wl_resource_get_user_data(res);
    }

    void unlinkFromParent(Subsurface& sub) {
        if (!sub.parent) {
            return;
        }

        removeOne(sub.parent->stackBelow, &sub);
        removeOne(sub.parent->stackAbove, &sub);
        sub.parent = nullptr;
    }

    void subsurfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void subsurfaceSetPosition(wl_client*, wl_resource* res, i32 x, i32 y) {
        Subsurface* sub = subFrom(res);

        if (!sub) {
            return;
        }

        sub->pendingX = x;
        sub->pendingY = y;
        sub->pendingPos = true;
    }

    // вставить относительно sibling'а; ref == сам родитель тоже валиден
    void subsurfaceRestack(Subsurface& sub, Surface* refSurface, bool above) {
        Surface* parent = sub.parent;

        if (!parent) {
            return;
        }

        removeOne(parent->stackBelow, &sub);
        removeOne(parent->stackAbove, &sub);

        if (refSurface == parent) {
            // относительно самого родителя
            if (above) {
                insertAt(parent->stackAbove, 0, &sub);
            } else {
                parent->stackBelow.pushBack(&sub);
            }

            return;
        }

        Subsurface* ref = refSurface->sub;
        Vector<Subsurface*>* stacks[] = {&parent->stackBelow, &parent->stackAbove};

        for (auto* stack : stacks) {
            long idx = indexOf(*stack, ref);

            if (idx >= 0) {
                insertAt(*stack, above ? (size_t)idx + 1 : (size_t)idx, &sub);

                return;
            }
        }

        // ref не sibling — по спеке ошибка протокола, прощаем и кладём наверх
        parent->stackAbove.pushBack(&sub);
    }

    void subsurfacePlaceAbove(wl_client*, wl_resource* res, wl_resource* sibling) {
        Subsurface* sub = subFrom(res);

        if (sub && sibling) {
            subsurfaceRestack(*sub, surfaceFrom(sibling), true);
        }
    }

    void subsurfacePlaceBelow(wl_client*, wl_resource* res, wl_resource* sibling) {
        Subsurface* sub = subFrom(res);

        if (sub && sibling) {
            subsurfaceRestack(*sub, surfaceFrom(sibling), false);
        }
    }

    void subsurfaceSetSync(wl_client*, wl_resource* res) {
        if (Subsurface* sub = subFrom(res)) {
            sub->sync = true;
        }
    }

    void subsurfaceSetDesync(wl_client*, wl_resource* res) {
        Subsurface* sub = subFrom(res);

        if (!sub) {
            return;
        }

        sub->sync = false;

        // переход в desync применяет накопленный кэш
        if (!sub->effectiveSync() && sub->cache.valid) {
            applySubsurfaceCache(*sub);
        }
    }

    const struct wl_subsurface_interface subsurfaceImpl = {
        .destroy = subsurfaceDestroy,
        .set_position = subsurfaceSetPosition,
        .place_above = subsurfacePlaceAbove,
        .place_below = subsurfacePlaceBelow,
        .set_sync = subsurfaceSetSync,
        .set_desync = subsurfaceSetDesync,
    };

    void subsurfaceResourceDestroyed(wl_resource* res) {
        Subsurface* sub = subFrom(res);

        if (!sub) {
            return;
        }

        Server* server = sub->surface ? sub->surface->server : nullptr;

        unlinkFromParent(*sub);

        for (wl_resource* cb : sub->cache.frames) {
            wl_resource_destroy(cb);
        }

        if (sub->surface) {
            sub->surface->sub = nullptr;
        }

        if (server) {
            impl(server).subsurfaceAlloc->release(sub);
        }
    }

    void subcompositorDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void subcompositorGetSubsurface(wl_client* client, wl_resource* res, u32 id,
                                    wl_resource* surfaceRes, wl_resource* parentRes) {
        auto* srv = (ServerImpl*)wl_resource_get_user_data(res);
        Surface* surface = surfaceFrom(surfaceRes);
        Surface* parent = surfaceFrom(parentRes);

        if (surface->xdg || surface->sub) {
            wl_resource_post_error(res, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                                   "у поверхности уже есть роль");

            return;
        }

        wl_resource* sres =
            wl_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(res), id);

        if (!sres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* sub = srv->subsurfaceAlloc->make();

        sub->surface = surface;
        sub->parent = parent;
        sub->res = sres;
        surface->sub = sub;
        parent->stackAbove.pushBack(sub); // новая субповерхность — наверху стека
        wl_resource_set_implementation(sres, &subsurfaceImpl, sub, subsurfaceResourceDestroyed);
    }

    const struct wl_subcompositor_interface subcompositorImpl = {
        .destroy = subcompositorDestroy,
        .get_subsurface = subcompositorGetSubsurface,
    };

    void subcompositorBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wl_subcompositor_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &subcompositorImpl, data, nullptr);
    }

    // ================= xdg_wm_base =================

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
        impl(server).toplevelAlloc->release(t);
    }

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

        ServerImpl& srv = impl(xs->server);
        auto* t = srv.toplevelAlloc->make();

        t->server = &srv;
        t->res = tres;
        t->xdg = xs;
        t->id = srv.nextToplevelId++;
        xs->toplevel = t;
        srv.toplevels.pushBack(t);
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

        impl(xs->server).xdgSurfaceAlloc->release(xs);
    }

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

        p->srv->positionerAlloc->release(p);
    }

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
        impl(server).popupAlloc->release(p);
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

        ServerImpl& srv = impl(xs->server);
        auto* p = srv.popupAlloc->make();

        p->server = &srv;
        p->res = pres;
        p->xdg = xs;
        p->parent = parentXs->surface;

        Positioner* pos = positionerFrom(positionerRes);

        pos->place(p->x, p->y);
        p->w = pos->w;
        p->h = pos->h;
        xs->popup = p;
        srv.popups.pushBack(p);
        wl_resource_set_implementation(pres, &popupImpl, p, popupResourceDestroyed);
    }

    void wmBaseDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void wmBaseCreatePositioner(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (ServerImpl*)wl_resource_get_user_data(res);
        wl_resource* pres = wl_resource_create(client, &xdg_positioner_interface,
                                               wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        Positioner* p = srv->positionerAlloc->make();

        p->srv = srv;
        wl_resource_set_implementation(pres, &positionerImpl, p, positionerResourceDestroyed);
    }

    void wmBaseGetXdgSurface(wl_client* client, wl_resource* res, u32 id, wl_resource* surfaceRes) {
        auto* srv = (ServerImpl*)wl_resource_get_user_data(res);
        auto* surface = (Surface*)wl_resource_get_user_data(surfaceRes);
        wl_resource* xres =
            wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(res), id);

        if (!xres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* xs = srv->xdgSurfaceAlloc->make();

        xs->server = srv;
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

    void xdgToplevelConfigureSize(Toplevel& t, int w, int h) {
        wl_array states;

        wl_array_init(&states);
        xdg_toplevel_send_configure(t.res, w, h, &states);
        wl_array_release(&states);
        xdg_surface_send_configure(t.xdg->res, wl_display_next_serial(t.server->display));
        t.cfgW = w;
        t.cfgH = h;
        sysO << "imway: configure "_sv << (const char*)t.title << " -> "_sv << w << "x"_sv << h
             << endL;
    }

    void xdgHandleCommit(Surface& s) {
        XdgSurface* xs = s.xdg;

        if (!xs) {
            return;
        }

        // Спека: первый configure отправляется в ответ на commit без буфера.
        if (!xs->initialConfigureSent) {
            if (s.hasContent) {
                sysE << "imway: client attached a buffer before configure (spec violation)"_sv
                     << endL;
            }

            sendConfigure(*xs);

            return;
        }

        if (xs->toplevel && !xs->toplevel->mapped && s.hasContent && xs->acked) {
            xs->toplevel->mapped = true;
            s.server->needsFrame = true;
            sysO << "imway: toplevel "_sv << (const char*)xs->toplevel->title << " ("_sv
                 << (const char*)xs->toplevel->appId << ") mapped "_sv << s.width << "x"_sv
                 << s.height << endL;

            if (s.server->seat) {
                s.server->seat->focusToplevel(xs->toplevel); // focus-on-map
            }
        }

        if (xs->toplevel && xs->toplevel->mapped && !s.hasContent) {
            xs->toplevel->mapped = false;
            s.server->needsFrame = true;
            sysO << "imway: toplevel "_sv << (const char*)xs->toplevel->title << " unmapped"_sv
                 << endL;
        }

        if (xs->popup && !xs->popup->mapped && s.hasContent && xs->acked) {
            xs->popup->mapped = true;
            s.server->needsFrame = true;
            sysO << "imway: popup mapped "_sv << s.width << "x"_sv << s.height << " at ("_sv
                 << xs->popup->x << ","_sv << xs->popup->y << ")"_sv
                 << (xs->popup->grab ? " grab" : "") << endL;

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

    // ================= wl_output =================

    void outputRelease(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    const struct wl_output_interface outputImpl = {.release = outputRelease};

    void outputBind(wl_client* client, void* data, u32 version, u32 id) {
        auto* server = (Server*)(ServerImpl*)data;
        wl_resource* res = wl_resource_create(client, &wl_output_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &outputImpl, server, nullptr);

        wl_output_send_geometry(res, 0, 0, 340, 210, WL_OUTPUT_SUBPIXEL_UNKNOWN, "imway",
                                "headless", WL_OUTPUT_TRANSFORM_NORMAL);
        wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, server->outW,
                            server->outH, (i32)(server->hz * 1000));

        if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
            wl_output_send_scale(res, 1);
        }

        if (version >= WL_OUTPUT_NAME_SINCE_VERSION) {
            wl_output_send_name(res, "HEADLESS-1");
        }

        if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
            wl_output_send_done(res);
        }
    }

    // ================= wl_data_device_manager (инертная заглушка) =================

    void sourceOffer(wl_client*, wl_resource*, const char*) {
    }

    void sourceSetActions(wl_client*, wl_resource*, u32) {
    }

    const struct wl_data_source_interface dataSourceImpl = {
        .offer = sourceOffer,
        .destroy = resDestroy,
        .set_actions = sourceSetActions,
    };

    void deviceStartDrag(wl_client*, wl_resource*, wl_resource*, wl_resource*, wl_resource*, u32) {
    }

    void deviceSetSelection(wl_client*, wl_resource*, wl_resource*, u32) {
    }

    const struct wl_data_device_interface dataDeviceImpl = {
        .start_drag = deviceStartDrag,
        .set_selection = deviceSetSelection,
        .release = resDestroy,
    };

    void managerCreateDataSource(wl_client* client, wl_resource* res, u32 id) {
        wl_resource* s = wl_resource_create(client, &wl_data_source_interface,
                                            wl_resource_get_version(res), id);

        if (s) {
            wl_resource_set_implementation(s, &dataSourceImpl, nullptr, nullptr);
        }
    }

    void managerGetDataDevice(wl_client* client, wl_resource* res, u32 id, wl_resource*) {
        wl_resource* d = wl_resource_create(client, &wl_data_device_interface,
                                            wl_resource_get_version(res), id);

        if (d) {
            wl_resource_set_implementation(d, &dataDeviceImpl, nullptr, nullptr);
        }
    }

    const struct wl_data_device_manager_interface dataManagerImpl = {
        .create_data_source = managerCreateDataSource,
        .get_data_device = managerGetDataDevice,
    };

    void dataManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res =
            wl_resource_create(client, &wl_data_device_manager_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &dataManagerImpl, data, nullptr);
    }

    // ================= zxdg_decoration_manager_v1 (всегда server_side) =================

    void decoSetMode(wl_client*, wl_resource* res, u32) {
        zxdg_toplevel_decoration_v1_send_configure(res,
                                                   ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    void decoUnsetMode(wl_client*, wl_resource* res) {
        zxdg_toplevel_decoration_v1_send_configure(res,
                                                   ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    const struct zxdg_toplevel_decoration_v1_interface decoImpl = {
        .destroy = resDestroy,
        .set_mode = decoSetMode,
        .unset_mode = decoUnsetMode,
    };

    void decoManagerGetToplevelDecoration(wl_client* client, wl_resource* res, u32 id,
                                          wl_resource* /*toplevel*/) {
        wl_resource* d = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface,
                                            wl_resource_get_version(res), id);

        if (!d) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(d, &decoImpl, nullptr, nullptr);
        zxdg_toplevel_decoration_v1_send_configure(d, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    const struct zxdg_decoration_manager_v1_interface decoManagerImpl = {
        .destroy = resDestroy,
        .get_toplevel_decoration = decoManagerGetToplevelDecoration,
    };

    void decoManagerBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res =
            wl_resource_create(client, &zxdg_decoration_manager_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &decoManagerImpl, data, nullptr);
    }

    // ================= wp_viewporter =================

    Surface* viewportSurfaceOf(wl_resource* res) {
        return (Surface*)wl_resource_get_user_data(res);
    }

    void viewportDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void viewportSetSource(wl_client*, wl_resource* res, wl_fixed_t x, wl_fixed_t y, wl_fixed_t w,
                           wl_fixed_t h) {
        Surface* s = viewportSurfaceOf(res);

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
        Surface* s = viewportSurfaceOf(res);

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
        Surface* s = viewportSurfaceOf(res);

        if (!s) {
            return;
        }

        // спека: состояние снимается на следующем commit
        s->vp.res = nullptr;
        s->vp.pendSw = s->vp.pendSh = -1;
        s->vp.pendDw = s->vp.pendDh = -1;
    }

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

    // ================= zwp_linux_dmabuf_v1 (v3) =================

    const struct wl_buffer_interface dmabufWlBufferImpl = {.destroy = resDestroy};

    void dmabufBufferResourceDestroyed(wl_resource* res) {
        auto* box = (BufferBox*)wl_resource_get_user_data(res);

        for (int i = 0; i < box->buf.nplanes; i++) {
            if (box->buf.fds[i] >= 0) {
                close(box->buf.fds[i]);
            }
        }

        box->srv->dmabufBoxAlloc->release(box);
    }

    Params* paramsFrom(wl_resource* res) {
        return (Params*)wl_resource_get_user_data(res);
    }

    void paramsDestroyResource(wl_resource* res) {
        Params* p = paramsFrom(res);

        if (p->pending) {
            for (int i = 0; i < kDmabufMaxPlanes; i++) {
                if (p->pending->buf.fds[i] >= 0) {
                    close(p->pending->buf.fds[i]);
                }
            }

            p->srv->dmabufBoxAlloc->release(p->pending);
        }

        p->srv->dmabufParamsAlloc->release(p);
    }

    void paramsAdd(wl_client*, wl_resource* res, i32 fd, u32 planeIdx, u32 offset, u32 stride,
                   u32 modifierHi, u32 modifierLo) {
        Params* p = paramsFrom(res);

        if (!p->pending) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                                   "params уже использованы");
            close(fd);

            return;
        }

        if (planeIdx >= (u32)kDmabufMaxPlanes) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                                   "plane_idx %u вне диапазона", planeIdx);
            close(fd);

            return;
        }

        DmabufBuffer& b = p->pending->buf;

        if (b.fds[planeIdx] >= 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                                   "plane %u уже задан", planeIdx);
            close(fd);

            return;
        }

        b.fds[planeIdx] = fd;
        b.offsets[planeIdx] = offset;
        b.strides[planeIdx] = stride;
        b.modifier = ((u64)modifierHi << 32) | modifierLo;

        if ((int)planeIdx + 1 > b.nplanes) {
            b.nplanes = planeIdx + 1;
        }
    }

    // общая часть create/create_immed; nullptr = протокол-ошибка уже послана
    wl_resource* paramsMakeBuffer(wl_client* client, wl_resource* res, u32 bufferId, i32 width,
                                  i32 height, u32 format) {
        Params* p = paramsFrom(res);

        if (!p->pending) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                                   "params уже использованы");

            return nullptr;
        }

        if (width <= 0 || height <= 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
                                   "размер %dx%d", width, height);

            return nullptr;
        }

        DmabufBuffer& b = p->pending->buf;

        if (b.nplanes == 0 || b.fds[0] < 0) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE, "нет plane 0");

            return nullptr;
        }

        if (!p->srv->renderer->dmabufFormatSupported(format, b.modifier)) {
            wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                                   "формат 0x%x не поддержан", format);

            return nullptr;
        }

        wl_resource* bres = wl_resource_create(client, &wl_buffer_interface, 1, bufferId);

        if (!bres) {
            wl_client_post_no_memory(client);

            return nullptr;
        }

        BufferBox* box = p->pending;

        p->pending = nullptr; // владение ушло в wl_buffer
        box->buf.width = width;
        box->buf.height = height;
        box->buf.format = format;
        wl_resource_set_implementation(bres, &dmabufWlBufferImpl, box,
                                       dmabufBufferResourceDestroyed);

        return bres;
    }

    void paramsCreate(wl_client* client, wl_resource* res, i32 width, i32 height, u32 format,
                      u32 /*flags*/) {
        wl_resource* buf = paramsMakeBuffer(client, res, 0, width, height, format);

        if (buf) {
            zwp_linux_buffer_params_v1_send_created(res, buf);
        } else if (!paramsFrom(res)->pending) {
            zwp_linux_buffer_params_v1_send_failed(res);
        }
    }

    void paramsCreateImmed(wl_client* client, wl_resource* res, u32 bufferId, i32 width,
                           i32 height, u32 format, u32 /*flags*/) {
        paramsMakeBuffer(client, res, bufferId, width, height, format);
    }

    const struct zwp_linux_buffer_params_v1_interface paramsImpl = {
        .destroy = resDestroy,
        .add = paramsAdd,
        .create = paramsCreate,
        .create_immed = paramsCreateImmed,
    };

    void dmabufCreateParams(wl_client* client, wl_resource* res, u32 id) {
        auto* srv = (ServerImpl*)wl_resource_get_user_data(res);
        wl_resource* pres = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                                               wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* p = srv->dmabufParamsAlloc->make();

        p->srv = srv;
        p->pending = srv->dmabufBoxAlloc->make();
        p->pending->srv = srv;
        wl_resource_set_implementation(pres, &paramsImpl, p, paramsDestroyResource);
    }

    void dmabufGetDefaultFeedback(wl_client*, wl_resource* res, u32) {
        wl_resource_post_error(res, WL_DISPLAY_ERROR_IMPLEMENTATION, "feedback (v4) не реализован");
    }

    void dmabufGetSurfaceFeedback(wl_client*, wl_resource* res, u32, wl_resource*) {
        wl_resource_post_error(res, WL_DISPLAY_ERROR_IMPLEMENTATION, "feedback (v4) не реализован");
    }

    const struct zwp_linux_dmabuf_v1_interface dmabufImpl = {
        .destroy = resDestroy,
        .create_params = dmabufCreateParams,
        .get_default_feedback = dmabufGetDefaultFeedback,
        .get_surface_feedback = dmabufGetSurfaceFeedback,
    };

    void dmabufBind(wl_client* client, void* data, u32 version, u32 id) {
        auto* srv = (ServerImpl*)data;
        wl_resource* res = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &dmabufImpl, srv, nullptr);

        // v1: format-события; v3: modifier-события
        Renderer* renderer = srv->renderer;

        for (size_t i = 0; i < renderer->dmabufFormatCount(); i++) {
            DmabufFormat fm = renderer->dmabufFormat(i);

            if (version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
                zwp_linux_dmabuf_v1_send_modifier(res, fm.fourcc, (u32)(fm.modifier >> 32),
                                                  (u32)(fm.modifier & 0xffffffff));
            } else {
                zwp_linux_dmabuf_v1_send_format(res, fm.fourcc);
            }
        }
    }
}

// ================= Positioner =================

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

// ================= ServerImpl =================

ServerImpl::ServerImpl(ObjPool* p, const ServerConfig& config)
    : pool(p)
    , cfg(config)
{
    outW = cfg.outW;
    outH = cfg.outH;
    hz = cfg.hz;

    display = wl_display_create();
    STD_VERIFY(display);

    wlLoop = wl_display_get_event_loop(display);
    loop = ev_default_loop(0);

    surfaceAlloc = pool->make<ObjList<Surface>>(pool);
    subsurfaceAlloc = pool->make<ObjList<Subsurface>>(pool);
    xdgSurfaceAlloc = pool->make<ObjList<XdgSurface>>(pool);
    toplevelAlloc = pool->make<ObjList<Toplevel>>(pool);
    popupAlloc = pool->make<ObjList<Popup>>(pool);
    regionAlloc = pool->make<ObjList<RegionBox>>(pool);
    positionerAlloc = pool->make<ObjList<Positioner>>(pool);
    dmabufBoxAlloc = pool->make<ObjList<BufferBox>>(pool);
    dmabufParamsAlloc = pool->make<ObjList<Params>>(pool);

    if (wl_display_add_socket(display, cfg.socketName) != 0) {
        Errno().raise(StringBuilder() << "wl socket "_sv << cfg.socketName
                                      << " failed (XDG_RUNTIME_DIR?)"_sv);
    }

    wl_display_init_shm(display);

    // kms до рендерера: размер output диктует режим дисплея
    if (!strcmp(cfg.backend, "kms")) {
        kms = Kms::create(pool, *this, cfg.drmDevice);
    }

    renderer = Renderer::create(pool, outW, outH);
    seat = Seat::create(pool, *this);

    if (kms) {
        STD_VERIFY(kms->start());

        try {
            input = InputLinux::create(pool, *this);
        } catch (...) {
            sysE << "imway: no input, mouse is dead: "_sv << Exception::current() << endL;
        }

        ImGui::GetIO().MouseDrawCursor = true; // композитный курсор
    }

    createGlobals();

    if (cfg.controlPath) {
        control = Control::create(pool, *this, cfg.controlPath);
    }

    ev_io_init(&wlIo, wlIoCb, wl_event_loop_get_fd(wlLoop), EV_READ);
    wlIo.data = this;
    ev_io_start(loop, &wlIo);

    ev_prepare_init(&flushPrepare, flushCb);
    flushPrepare.data = this;
    ev_prepare_start(loop, &flushPrepare);

    ev_timer_init(&frameTimer, frameCb, 0., 1.0 / hz);
    frameTimer.data = this;
    ev_timer_start(loop, &frameTimer);

    ev_signal_init(&sigInt, signalCb, SIGINT);
    ev_signal_start(loop, &sigInt);
    ev_signal_init(&sigTerm, signalCb, SIGTERM);
    ev_signal_start(loop, &sigTerm);
    watchersStarted = true;

    sysO << "imway: socket "_sv << cfg.socketName << ", output "_sv << outW << "x"_sv << outH
         << "@"_sv << (i64)hz << endL;
}

ServerImpl::~ServerImpl() noexcept {
    if (watchersStarted) {
        ev_io_stop(loop, &wlIo);
        ev_prepare_stop(loop, &flushPrepare);
        ev_timer_stop(loop, &frameTimer);
        ev_signal_stop(loop, &sigInt);
        ev_signal_stop(loop, &sigTerm);
    }

    // нормальный путь гасит display в run(); сюда попадаем только при
    // исключении из конструктора — клиентов ещё нет
    if (display) {
        wl_display_destroy(display);
        display = nullptr;
    }
}

void ServerImpl::createGlobals() {
    wl_global_create(display, &wl_compositor_interface, 4, this, compositorBind);
    wl_global_create(display, &wl_subcompositor_interface, 1, this, subcompositorBind);
    // v3: repositioned-событие у попапов
    wl_global_create(display, &xdg_wm_base_interface, 3, this, wmBaseBind);
    wl_global_create(display, &wl_output_interface, 4, this, outputBind);
    seatCreateGlobal(*this);
    wl_global_create(display, &wl_data_device_manager_interface, 3, this, dataManagerBind);
    wl_global_create(display, &zxdg_decoration_manager_v1_interface, 1, this, decoManagerBind);
    wl_global_create(display, &wp_viewporter_interface, 1, this, viewporterBind);

    if (renderer->dmabufFormatCount() > 0) {
        wl_global_create(display, &zwp_linux_dmabuf_v1_interface, 3, this, dmabufBind);
    } else {
        sysE << "imway: vulkan lacks dmabuf import, linux_dmabuf global not created"_sv << endL;
    }
}

void ServerImpl::dismissPopup(Popup& p) {
    xdgPopupDismiss(p);
}

void ServerImpl::onFrameTick() {
    // lavapipe — это CPU: без изменений кадр не рисуем вовсе
    if (needsFrame) {
        settleFrames = 3; // ImGui дорисует hover/анимации
    }

    bool active = needsFrame || settleFrames > 0;

    needsFrame = false;

    if (active) {
        settleFrames--;

        // загрузить свежие пиксели в текстуры (субповерхности тоже — у каждой своя)
        for (Surface* s : surfaces) {
            if (s->dirty && s->hasContent) {
                if (s->dmabufBuffer) {
                    renderer->importDmabuf(*s);
                } else {
                    renderer->uploadSurface(*s);
                }

                s->dirty = false;
            }
        }

        renderer->renderFrame(*this);

        if (kms) {
            kms->present(renderer->readbackData());
        }

        // frame callbacks — всем деревьям, показанным в кадре (попапам тоже,
        // GTK не рисует контент меню, пока не получит frame done)
        u32 t = nowMsec();

        for (Toplevel* tl : toplevels) {
            Surface* surf = tl->xdg ? tl->xdg->surface : nullptr;

            if (tl->mapped && surf) {
                fireFrameCallbacks(*surf, t);
            }
        }

        for (Popup* p : popups) {
            Surface* surf = p->xdg ? p->xdg->surface : nullptr;

            if (surf) {
                fireFrameCallbacks(*surf, t);
            }
        }

        // ресайз ImGui-окном: контент-регион разошёлся с размером поверхности
        for (Toplevel* tl : toplevels) {
            Surface* surf = tl->xdg ? tl->xdg->surface : nullptr;

            if (!tl->mapped || !surf || tl->desiredW <= 0) {
                continue;
            }

            bool differsView = tl->desiredW != surf->viewW() || tl->desiredH != surf->viewH();
            bool differsSent = tl->desiredW != tl->cfgW || tl->desiredH != tl->cfgH;

            if (differsView && differsSent) {
                xdgToplevelConfigureSize(*tl, tl->desiredW, tl->desiredH);
            }
        }
    }

    framesDone++;

    if (cfg.framesLimit > 0 && framesDone >= cfg.framesLimit) {
        ev_break(loop, EVBREAK_ALL);
    }
}

void ServerImpl::run() {
    ev_run(loop, 0);

    // скриншот последнего кадра — до разрушения клиентов и рендерера
    if (cfg.screenshotPath && renderer) {
        renderer->screenshot(cfg.screenshotPath);
        sysO << "imway: screenshot: "_sv << cfg.screenshotPath << endL;
    }

    // клиенты умирают первыми: их деструкторы освобождают текстуры через renderer;
    // сами подсистемы умрут вместе с пулом (LIFO: control → input → seat → renderer → kms)
    wl_display_destroy_clients(display);
    wl_display_destroy(display);
    display = nullptr;
}

// ================= публичные определения =================

Server::~Server() noexcept {
}

Server* Server::create(ObjPool* pool, const ServerConfig& config) {
    return pool->make<ServerImpl>(pool, config);
}

DmabufBuffer* dmabufFromBufferResource(wl_resource* res) {
    if (!wl_resource_instance_of(res, &wl_buffer_interface, &dmabufWlBufferImpl)) {
        return nullptr;
    }

    return &((BufferBox*)wl_resource_get_user_data(res))->buf;
}

u32 nowMsec() {
    timespec ts{};

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (u32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// --- методы модели ---

bool Surface::inputContains(double sx, double sy) const {
    if (!inputRegionSet) {
        return true;
    }

    for (const RectI& r : inputRegion) {
        if (sx >= r.x && sy >= r.y && sx < r.x + r.w && sy < r.y + r.h) {
            return true;
        }
    }

    return false;
}

int Surface::viewW() const {
    return vp.hasDst ? vp.dw : vp.hasSrc ? (int)vp.sw : width;
}

int Surface::viewH() const {
    return vp.hasDst ? vp.dh : vp.hasSrc ? (int)vp.sh : height;
}

Surface* Surface::rootSurface() {
    Surface* s = this;

    // вверх по цепочке субповерхностей до корня
    while (s->sub && s->sub->parent) {
        s = s->sub->parent;
    }

    return s;
}

Toplevel* Surface::rootToplevel() {
    Surface* s = rootSurface();

    if (s->sub) { // сирота: родитель умер
        return nullptr;
    }

    return s->xdg ? s->xdg->toplevel : nullptr;
}

bool Subsurface::effectiveSync() const {
    for (const Subsurface* s = this; s; s = s->parent ? s->parent->sub : nullptr) {
        if (s->sync) {
            return true;
        }
    }

    return false;
}
