// Ядро композитора: event loop, все wayland-глобалы и обработчики протоколов.
// Протокольные части модели сцены (pending-состояние, кэши, xdg-ресурсы) —
// impl-наследники структур сцены, наружу не видны.

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
    struct PopupImpl;
    struct ServerImpl;
    struct SurfaceImpl;
    struct ToplevelImpl;

    // роль xdg_surface — чисто протокольная, сцене не видна
    struct XdgSurface {
        ServerImpl* srv = nullptr;
        wl_resource* res = nullptr;
        SurfaceImpl* surface = nullptr;
        ToplevelImpl* toplevel = nullptr;
        PopupImpl* popup = nullptr;
        bool initialConfigureSent = false;
        bool acked = false;
    };

    struct SurfaceImpl: public Surface {
        ServerImpl* srv = nullptr;

        // pending-состояние (double buffering протокола)
        struct {
            wl_resource* buffer = nullptr;
            bool newlyAttached = false;
            wl_listener bufferDestroy{};
            bool bufferDestroyArmed = false;
            Vector<wl_resource*> frames;
            bool inputRegionSet = false; // false = вся поверхность
            Vector<RectI> inputRegion;
        } pending;

        bool dirty = false; // контент изменился с последней загрузки в текстуру
        Vector<wl_resource*> frameCbs;

        // dmabuf-контент: буфер держим до замены (рендер читает память напрямую)
        wl_resource* dmabufRes = nullptr;
        wl_listener dmabufDestroy{};
        bool dmabufDestroyArmed = false;

        // wp_viewport: pending (-1 = unset, применяется на commit) и живой ресурс
        wl_resource* vpRes = nullptr;
        double pendSx = -1, pendSy = -1, pendSw = -1, pendSh = -1;
        int pendDw = -1, pendDh = -1;

        XdgSurface* xdg = nullptr; // роль xdg_surface
    };

    struct SubsurfaceImpl: public Subsurface {
        ServerImpl* srv = nullptr;
        wl_resource* res = nullptr;

        int pendingX = 0, pendingY = 0;
        bool pendingPos = false; // применяется на commit родителя
        bool sync = true;        // режим по умолчанию — synchronized

        // кэш состояния для sync-коммитов (применяется на commit родителя)
        struct {
            bool valid = false;
            bool hasContent = false;
            int width = 0, height = 0;
            Vector<u8> pixels;
            Vector<wl_resource*> frames;
        } cache;

        bool effectiveSync() const; // sync у себя или у любого предка-субповерхности
    };

    struct ToplevelImpl: public Toplevel {
        ServerImpl* srv = nullptr;
        wl_resource* res = nullptr;
        XdgSurface* xdg = nullptr;
        int cfgW = 0, cfgH = 0; // последний отправленный configure
    };

    struct PopupImpl: public Popup {
        ServerImpl* srv = nullptr;
        wl_resource* res = nullptr;
        XdgSurface* xdg = nullptr;
        int w = 0, h = 0; // размер из позиционера
    };

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

        struct ev_loop* loop = nullptr;
        wl_event_loop* wlLoop = nullptr;

        Renderer* renderer = nullptr;
        Seat* seat = nullptr;
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
        ObjList<SurfaceImpl>* surfaceAlloc = nullptr;
        ObjList<SubsurfaceImpl>* subsurfaceAlloc = nullptr;
        ObjList<XdgSurface>* xdgSurfaceAlloc = nullptr;
        ObjList<ToplevelImpl>* toplevelAlloc = nullptr;
        ObjList<PopupImpl>* popupAlloc = nullptr;
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

    SubsurfaceImpl& impl(Subsurface* sub) {
        return *(SubsurfaceImpl*)sub;
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

    void fireFrameCallbacks(SurfaceImpl& s, u32 t) {
        // деструктор ресурса удаляет callback из frameCbs — забираем список до итерации
        Vector<wl_resource*> cbs;

        cbs.xchg(s.frameCbs);

        for (wl_resource* cb : cbs) {
            wl_callback_send_done(cb, t);
            wl_resource_destroy(cb);
        }

        for (Subsurface* c : s.stackBelow) {
            if (c->surface) {
                fireFrameCallbacks(*(SurfaceImpl*)c->surface, t);
            }
        }

        for (Subsurface* c : s.stackAbove) {
            if (c->surface) {
                fireFrameCallbacks(*(SurfaceImpl*)c->surface, t);
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
    void unlinkFromParent(SubsurfaceImpl&);
    void applySubsurfaceCache(SubsurfaceImpl&);
    void xdgHandleCommit(SurfaceImpl&);
    void xdgPopupDismiss(PopupImpl&);
    void viewportApplyPending(SurfaceImpl&);
    void viewportSurfaceGone(SurfaceImpl&);
    DmabufBuffer* dmabufFromRes(wl_resource*);

    // ================= wl_surface / wl_region / wl_subcompositor =================

    SurfaceImpl* surfaceFrom(wl_resource* res) {
        return (SurfaceImpl*)wl_resource_get_user_data(res);
    }

    void detachPendingBuffer(SurfaceImpl& s) {
        if (s.pending.bufferDestroyArmed) {
            wl_list_remove(&s.pending.bufferDestroy.link);
            s.pending.bufferDestroyArmed = false;
        }

        s.pending.buffer = nullptr;
    }

    void pendingBufferDestroyed(wl_listener* l, void*) {
        SurfaceImpl* s = wl_container_of(l, s, pending.bufferDestroy);

        s->pending.buffer = nullptr;
        s->pending.bufferDestroyArmed = false;
        wl_list_remove(&s->pending.bufferDestroy.link);
    }

    // --- удержание dmabuf-буфера (рендер читает его память напрямую) ---

    void heldDmabufDestroyed(wl_listener* l, void*) {
        SurfaceImpl* s = wl_container_of(l, s, dmabufDestroy);

        // клиент уничтожил буфер, пока тот показан: текстура уже импортирована
        // (память живёт на нашем fd-дубликате), просто забываем ресурс
        s->dmabuf = nullptr;
        s->dmabufRes = nullptr;
        s->dmabufDestroyArmed = false;
        wl_list_remove(&s->dmabufDestroy.link);
    }

    void releaseHeldDmabuf(SurfaceImpl& s) {
        if (!s.dmabufRes) {
            return;
        }

        wl_buffer_send_release(s.dmabufRes);

        if (s.dmabufDestroyArmed) {
            wl_list_remove(&s.dmabufDestroy.link);
            s.dmabufDestroyArmed = false;
        }

        s.dmabuf = nullptr;
        s.dmabufRes = nullptr;
    }

    void holdDmabuf(SurfaceImpl& s, wl_resource* buffer, DmabufBuffer* buf) {
        releaseHeldDmabuf(s);

        s.dmabuf = buf;
        s.dmabufRes = buffer;
        s.dmabufDestroy.notify = heldDmabufDestroyed;
        wl_resource_add_destroy_listener(buffer, &s.dmabufDestroy);
        s.dmabufDestroyArmed = true;
    }

    void surfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void surfaceAttach(wl_client*, wl_resource* res, wl_resource* buffer, i32, i32) {
        SurfaceImpl& s = *surfaceFrom(res);

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
        SurfaceImpl* s = (SurfaceImpl*)wl_resource_get_user_data(cb);

        if (!s) {
            return;
        }

        removeOne(s->pending.frames, cb);
        removeOne(s->frameCbs, cb);
    }

    void surfaceFrame(wl_client* client, wl_resource* res, u32 id) {
        SurfaceImpl& s = *surfaceFrom(res);
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
        SurfaceImpl& s = *surfaceFrom(res);

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

    void copyShmBuffer(SurfaceImpl& s, wl_shm_buffer* shm) {
        copyShmBufferTo(*shm, s.width, s.height, s.pixels);

        if (s.width > 0) {
            s.dirty = true;
            s.hasContent = true;
        }
    }

    void applyChildrenCaches(SurfaceImpl& s) {
        Vector<Subsurface*>* stacks[] = {&s.stackBelow, &s.stackAbove};

        for (auto* stack : stacks) {
            for (Subsurface* c : *stack) {
                SubsurfaceImpl& sub = impl(c);

                // позиция двойнобуферизована коммитом родителя для любых детей
                if (sub.pendingPos) {
                    sub.x = sub.pendingX;
                    sub.y = sub.pendingY;
                    sub.pendingPos = false;
                }

                if (sub.sync) {
                    applySubsurfaceCache(sub);
                }
            }
        }
    }

    // применить кэш sync-субповерхности и рекурсивно кэши её sync-детей
    void applySubsurfaceCache(SubsurfaceImpl& sub) {
        if (sub.cache.valid) {
            SurfaceImpl& s = *(SurfaceImpl*)sub.surface;

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
            if (impl(c).sync) {
                applySubsurfaceCache(impl(c));
            }
        }

        for (Subsurface* c : sub.surface->stackAbove) {
            if (impl(c).sync) {
                applySubsurfaceCache(impl(c));
            }
        }
    }

    void surfaceCommit(wl_client*, wl_resource* res) {
        SurfaceImpl& s = *surfaceFrom(res);

        s.srv->scene.needsFrame = true;

        SubsurfaceImpl* sub = (SubsurfaceImpl*)s.sub;
        bool toCache = sub && sub->effectiveSync();

        if (s.pending.newlyAttached) {
            if (!s.pending.buffer) {
                if (toCache) {
                    sub->cache.valid = true;
                    sub->cache.hasContent = false;
                    sub->cache.width = sub->cache.height = 0;
                    sub->cache.pixels.clear();
                } else {
                    s.hasContent = false;
                    s.width = s.height = 0;
                }
            } else if (wl_shm_buffer* shm = wl_shm_buffer_get(s.pending.buffer)) {
                if (toCache) {
                    // снимаем копию сразу (буфер возвращается клиенту), показ — на commit родителя
                    copyShmBufferTo(*shm, sub->cache.width, sub->cache.height, sub->cache.pixels);
                    sub->cache.hasContent = sub->cache.width > 0;
                    sub->cache.valid = true;
                } else {
                    copyShmBuffer(s, shm);
                }

                wl_buffer_send_release(s.pending.buffer);
                releaseHeldDmabuf(s); // на случай смены dmabuf → shm
            } else if (DmabufBuffer* db = dmabufFromRes(s.pending.buffer)) {
                // dmabuf применяем сразу даже для sync-субповерхностей (без кэша):
                // буфер один, копий нет — упрощение
                holdDmabuf(s, s.pending.buffer, db);
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
                sub->cache.frames.pushBack(cb);
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
        SurfaceImpl* s = surfaceFrom(res);
        ServerImpl* srv = s->srv;

        detachPendingBuffer(*s);

        for (wl_resource* cb : s->pending.frames) {
            wl_resource_set_user_data(cb, nullptr);
        }

        for (wl_resource* cb : s->frameCbs) {
            wl_resource_set_user_data(cb, nullptr);
        }

        if (s->xdg) {
            s->xdg->surface = nullptr;

            if (s->xdg->toplevel) {
                s->xdg->toplevel->surface = nullptr;
            }

            if (s->xdg->popup) {
                s->xdg->popup->surface = nullptr;
            }
        }

        if (s->sub) { // роль-субповерхность: выпасть из стека родителя
            unlinkFromParent(impl(s->sub));
            s->sub->surface = nullptr;
        }

        for (Subsurface* c : s->stackBelow) { // дети-сироты не рендерятся
            c->parent = nullptr;
        }

        for (Subsurface* c : s->stackAbove) {
            c->parent = nullptr;
        }

        for (Popup* p : srv->scene.popups) { // попапы умершего родителя гаснут
            if (p->parent == s) {
                p->parent = nullptr;

                if (p->mapped) {
                    xdgPopupDismiss(*(PopupImpl*)p);
                }
            }
        }

        if (srv->seat) {
            srv->seat->surfaceGone(s);
        }

        releaseHeldDmabuf(*s);
        viewportSurfaceGone(*s);

        if (s->texture && srv->renderer) {
            srv->renderer->destroyTexture(s->texture);
        }

        srv->scene.needsFrame = true;
        removeOne(srv->scene.surfaces, (Surface*)s);
        srv->surfaceAlloc->release(s);
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

        s->srv = srv;
        s->res = sres;
        srv->scene.surfaces.pushBack(s);
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

    SubsurfaceImpl* subFrom(wl_resource* res) {
        return (SubsurfaceImpl*)wl_resource_get_user_data(res);
    }

    void unlinkFromParent(SubsurfaceImpl& sub) {
        if (!sub.parent) {
            return;
        }

        removeOne(sub.parent->stackBelow, (Subsurface*)&sub);
        removeOne(sub.parent->stackAbove, (Subsurface*)&sub);
        sub.parent = nullptr;
    }

    void subsurfaceDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void subsurfaceSetPosition(wl_client*, wl_resource* res, i32 x, i32 y) {
        SubsurfaceImpl* sub = subFrom(res);

        if (!sub) {
            return;
        }

        sub->pendingX = x;
        sub->pendingY = y;
        sub->pendingPos = true;
    }

    // вставить относительно sibling'а; ref == сам родитель тоже валиден
    void subsurfaceRestack(SubsurfaceImpl& sub, Surface* refSurface, bool above) {
        Surface* parent = sub.parent;

        if (!parent) {
            return;
        }

        removeOne(parent->stackBelow, (Subsurface*)&sub);
        removeOne(parent->stackAbove, (Subsurface*)&sub);

        if (refSurface == parent) {
            // относительно самого родителя
            if (above) {
                insertAt(parent->stackAbove, 0, (Subsurface*)&sub);
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
                insertAt(*stack, above ? (size_t)idx + 1 : (size_t)idx, (Subsurface*)&sub);

                return;
            }
        }

        // ref не sibling — по спеке ошибка протокола, прощаем и кладём наверх
        parent->stackAbove.pushBack(&sub);
    }

    void subsurfacePlaceAbove(wl_client*, wl_resource* res, wl_resource* sibling) {
        SubsurfaceImpl* sub = subFrom(res);

        if (sub && sibling) {
            subsurfaceRestack(*sub, surfaceFrom(sibling), true);
        }
    }

    void subsurfacePlaceBelow(wl_client*, wl_resource* res, wl_resource* sibling) {
        SubsurfaceImpl* sub = subFrom(res);

        if (sub && sibling) {
            subsurfaceRestack(*sub, surfaceFrom(sibling), false);
        }
    }

    void subsurfaceSetSync(wl_client*, wl_resource* res) {
        if (SubsurfaceImpl* sub = subFrom(res)) {
            sub->sync = true;
        }
    }

    void subsurfaceSetDesync(wl_client*, wl_resource* res) {
        SubsurfaceImpl* sub = subFrom(res);

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
        SubsurfaceImpl* sub = subFrom(res);

        if (!sub) {
            return;
        }

        unlinkFromParent(*sub);

        for (wl_resource* cb : sub->cache.frames) {
            wl_resource_destroy(cb);
        }

        if (sub->surface) {
            sub->surface->sub = nullptr;
        }

        sub->srv->subsurfaceAlloc->release(sub);
    }

    void subcompositorDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void subcompositorGetSubsurface(wl_client* client, wl_resource* res, u32 id,
                                    wl_resource* surfaceRes, wl_resource* parentRes) {
        auto* srv = (ServerImpl*)wl_resource_get_user_data(res);
        SurfaceImpl* surface = surfaceFrom(surfaceRes);
        SurfaceImpl* parent = surfaceFrom(parentRes);

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

        sub->srv = srv;
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
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);

        copyBounded(t->title, sizeof(t->title), title);
    }

    void toplevelSetAppId(wl_client*, wl_resource* res, const char* appId) {
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);

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
        auto* t = (ToplevelImpl*)wl_resource_get_user_data(res);
        ServerImpl* srv = t->srv;

        if (srv->seat) {
            srv->seat->toplevelGone(t);
        }

        if (t->xdg) {
            t->xdg->toplevel = nullptr;
        }

        if (t->surface) {
            t->surface->toplevel = nullptr;
        }

        removeOne(srv->scene.toplevels, (Toplevel*)t);
        sysO << "imway: toplevel "_sv << (const char*)t->title << " destroyed"_sv << endL;
        srv->scene.needsFrame = true;
        srv->toplevelAlloc->release(t);
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
            PopupImpl& p = *xs.popup;

            xdg_popup_send_configure(p.res, p.x, p.y, p.w, p.h);
        }

        xdg_surface_send_configure(xs.res, wl_display_next_serial(xs.srv->display));
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

        ServerImpl* srv = xs->srv;
        auto* t = srv->toplevelAlloc->make();

        t->srv = srv;
        t->res = tres;
        t->xdg = xs;
        t->surface = xs->surface;
        t->id = srv->nextToplevelId++;
        xs->toplevel = t;

        if (xs->surface) {
            xs->surface->toplevel = t;
        }

        srv->scene.toplevels.pushBack(t);
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
            xs->surface->toplevel = nullptr;
        }

        if (xs->toplevel) {
            xs->toplevel->xdg = nullptr;
            xs->toplevel->surface = nullptr;
        }

        if (xs->popup) {
            xs->popup->xdg = nullptr;
            xs->popup->surface = nullptr;
        }

        xs->srv->xdgSurfaceAlloc->release(xs);
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
        auto* p = (PopupImpl*)wl_resource_get_user_data(res);

        p->grab = true;
    }

    void popupReposition(wl_client*, wl_resource* res, wl_resource* positioner, u32 token) {
        auto* p = (PopupImpl*)wl_resource_get_user_data(res);
        Positioner* pos = positionerFrom(positioner);

        pos->place(p->x, p->y);
        p->w = pos->w;
        p->h = pos->h;
        xdg_popup_send_repositioned(res, token);
        xdg_popup_send_configure(res, p->x, p->y, p->w, p->h);
        xdg_surface_send_configure(p->xdg->res, wl_display_next_serial(p->srv->display));
        p->srv->scene.needsFrame = true;
    }

    const struct xdg_popup_interface popupImpl = {
        .destroy = popupDestroy,
        .grab = popupGrab,
        .reposition = popupReposition,
    };

    void popupResourceDestroyed(wl_resource* res) {
        auto* p = (PopupImpl*)wl_resource_get_user_data(res);
        ServerImpl* srv = p->srv;

        if (srv->seat) {
            srv->seat->popupGone(p);
        }

        if (p->xdg) {
            p->xdg->popup = nullptr;
        }

        removeOne(srv->scene.popups, (Popup*)p);
        srv->scene.needsFrame = true;
        srv->popupAlloc->release(p);
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

        ServerImpl* srv = xs->srv;
        auto* p = srv->popupAlloc->make();

        p->srv = srv;
        p->res = pres;
        p->xdg = xs;
        p->surface = xs->surface;
        p->parent = parentXs->surface;

        Positioner* pos = positionerFrom(positionerRes);

        pos->place(p->x, p->y);
        p->w = pos->w;
        p->h = pos->h;
        xs->popup = p;
        srv->scene.popups.pushBack(p);
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
        auto* surface = surfaceFrom(surfaceRes);
        wl_resource* xres =
            wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(res), id);

        if (!xres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* xs = srv->xdgSurfaceAlloc->make();

        xs->srv = srv;
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

    void xdgToplevelConfigureSize(ToplevelImpl& t, int w, int h) {
        wl_array states;

        wl_array_init(&states);
        xdg_toplevel_send_configure(t.res, w, h, &states);
        wl_array_release(&states);
        xdg_surface_send_configure(t.xdg->res, wl_display_next_serial(t.srv->display));
        t.cfgW = w;
        t.cfgH = h;
        sysO << "imway: configure "_sv << (const char*)t.title << " -> "_sv << w << "x"_sv << h
             << endL;
    }

    void xdgHandleCommit(SurfaceImpl& s) {
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
            s.srv->scene.needsFrame = true;
            sysO << "imway: toplevel "_sv << (const char*)xs->toplevel->title << " ("_sv
                 << (const char*)xs->toplevel->appId << ") mapped "_sv << s.width << "x"_sv
                 << s.height << endL;

            if (s.srv->seat) {
                s.srv->seat->focusToplevel(xs->toplevel); // focus-on-map
            }
        }

        if (xs->toplevel && xs->toplevel->mapped && !s.hasContent) {
            xs->toplevel->mapped = false;
            s.srv->scene.needsFrame = true;
            sysO << "imway: toplevel "_sv << (const char*)xs->toplevel->title << " unmapped"_sv
                 << endL;
        }

        if (xs->popup && !xs->popup->mapped && s.hasContent && xs->acked) {
            xs->popup->mapped = true;
            s.srv->scene.needsFrame = true;
            sysO << "imway: popup mapped "_sv << s.width << "x"_sv << s.height << " at ("_sv
                 << xs->popup->x << ","_sv << xs->popup->y << ")"_sv
                 << (xs->popup->grab ? " grab" : "") << endL;

            if (xs->popup->grab && s.srv->seat) {
                s.srv->seat->popupGrabStart(xs->popup);
            }
        }

        if (xs->popup && xs->popup->mapped && !s.hasContent) {
            xs->popup->mapped = false;
            s.srv->scene.needsFrame = true;
        }
    }

    void xdgPopupDismiss(PopupImpl& p) {
        if (!p.mapped) {
            return;
        }

        p.mapped = false;
        p.srv->scene.needsFrame = true;

        if (p.srv->seat) {
            p.srv->seat->popupGone(&p);
        }

        xdg_popup_send_popup_done(p.res);
    }

    // ================= wl_output =================

    void outputRelease(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    const struct wl_output_interface outputImpl = {.release = outputRelease};

    void outputBind(wl_client* client, void* data, u32 version, u32 id) {
        auto* srv = (ServerImpl*)data;
        wl_resource* res = wl_resource_create(client, &wl_output_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &outputImpl, srv, nullptr);

        wl_output_send_geometry(res, 0, 0, 340, 210, WL_OUTPUT_SUBPIXEL_UNKNOWN, "imway",
                                "headless", WL_OUTPUT_TRANSFORM_NORMAL);
        wl_output_send_mode(res, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                            srv->scene.outW, srv->scene.outH, (i32)(srv->scene.hz * 1000));

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

    void viewportDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void viewportSetSource(wl_client*, wl_resource* res, wl_fixed_t x, wl_fixed_t y, wl_fixed_t w,
                           wl_fixed_t h) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "поверхность уничтожена");

            return;
        }

        double dx = wl_fixed_to_double(x), dy = wl_fixed_to_double(y);
        double dw = wl_fixed_to_double(w), dh = wl_fixed_to_double(h);

        if (dx == -1 && dy == -1 && dw == -1 && dh == -1) { // unset
            s->pendSw = s->pendSh = -1;

            return;
        }

        if (dx < 0 || dy < 0 || dw <= 0 || dh <= 0) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "некорректный source rect");

            return;
        }

        s->pendSx = dx;
        s->pendSy = dy;
        s->pendSw = dw;
        s->pendSh = dh;
    }

    void viewportSetDestination(wl_client*, wl_resource* res, i32 w, i32 h) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_NO_SURFACE, "поверхность уничтожена");

            return;
        }

        if (w == -1 && h == -1) { // unset
            s->pendDw = s->pendDh = -1;

            return;
        }

        if (w <= 0 || h <= 0) {
            wl_resource_post_error(res, WP_VIEWPORT_ERROR_BAD_VALUE, "некорректный destination");

            return;
        }

        s->pendDw = w;
        s->pendDh = h;
    }

    const struct wp_viewport_interface viewportImpl = {
        .destroy = viewportDestroy,
        .set_source = viewportSetSource,
        .set_destination = viewportSetDestination,
    };

    void viewportResourceDestroyed(wl_resource* res) {
        SurfaceImpl* s = surfaceFrom(res);

        if (!s) {
            return;
        }

        // спека: состояние снимается на следующем commit
        s->vpRes = nullptr;
        s->pendSw = s->pendSh = -1;
        s->pendDw = s->pendDh = -1;
    }

    void viewporterDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void viewporterGetViewport(wl_client* client, wl_resource* res, u32 id,
                               wl_resource* surfaceRes) {
        SurfaceImpl* s = surfaceFrom(surfaceRes);

        if (s->vpRes) {
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

        s->vpRes = vres;
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

    void viewportApplyPending(SurfaceImpl& s) {
        s.vp.hasSrc = s.pendSw > 0;

        if (s.vp.hasSrc) {
            s.vp.sx = s.pendSx;
            s.vp.sy = s.pendSy;
            s.vp.sw = s.pendSw;
            s.vp.sh = s.pendSh;
        }

        s.vp.hasDst = s.pendDw > 0;

        if (s.vp.hasDst) {
            s.vp.dw = s.pendDw;
            s.vp.dh = s.pendDh;
        }
    }

    // при уничтожении поверхности вьюпорт становится инертным
    void viewportSurfaceGone(SurfaceImpl& s) {
        if (s.vpRes) {
            wl_resource_set_user_data(s.vpRes, nullptr);
        }
    }

    // ================= zwp_linux_dmabuf_v1 (v3) =================

    const struct wl_buffer_interface dmabufWlBufferImpl = {.destroy = resDestroy};

    DmabufBuffer* dmabufFromRes(wl_resource* res) {
        if (!wl_resource_instance_of(res, &wl_buffer_interface, &dmabufWlBufferImpl)) {
            return nullptr;
        }

        return &((BufferBox*)wl_resource_get_user_data(res))->buf;
    }

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

// ================= методы impl-структур =================

bool SubsurfaceImpl::effectiveSync() const {
    for (const SubsurfaceImpl* s = this; s;
         s = s->parent ? (const SubsurfaceImpl*)s->parent->sub : nullptr) {
        if (s->sync) {
            return true;
        }
    }

    return false;
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

// ================= ServerImpl =================

ServerImpl::ServerImpl(ObjPool* p, const ServerConfig& config)
    : pool(p)
    , cfg(config)
{
    scene.outW = cfg.outW;
    scene.outH = cfg.outH;
    scene.hz = cfg.hz;

    display = wl_display_create();
    STD_VERIFY(display);

    wlLoop = wl_display_get_event_loop(display);
    loop = ev_default_loop(0);

    surfaceAlloc = pool->make<ObjList<SurfaceImpl>>(pool);
    subsurfaceAlloc = pool->make<ObjList<SubsurfaceImpl>>(pool);
    xdgSurfaceAlloc = pool->make<ObjList<XdgSurface>>(pool);
    toplevelAlloc = pool->make<ObjList<ToplevelImpl>>(pool);
    popupAlloc = pool->make<ObjList<PopupImpl>>(pool);
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
        kms = Kms::create(pool, loop, cfg.drmDevice);
        scene.outW = kms->width();
        scene.outH = kms->height();
        scene.hz = kms->refresh();
    }

    renderer = Renderer::create(pool, scene.outW, scene.outH);
    seat = Seat::create(pool, *this);

    if (kms) {
        STD_VERIFY(kms->start());

        try {
            input = InputLinux::create(pool, loop, *seat, scene.outW, scene.outH);
        } catch (...) {
            sysE << "imway: no input, mouse is dead: "_sv << Exception::current() << endL;
        }

        ImGui::GetIO().MouseDrawCursor = true; // композитный курсор
    }

    createGlobals();

    if (cfg.controlPath) {
        control = Control::create(pool, loop, *seat, *renderer, cfg.controlPath);
    }

    ev_io_init(&wlIo, wlIoCb, wl_event_loop_get_fd(wlLoop), EV_READ);
    wlIo.data = this;
    ev_io_start(loop, &wlIo);

    ev_prepare_init(&flushPrepare, flushCb);
    flushPrepare.data = this;
    ev_prepare_start(loop, &flushPrepare);

    ev_timer_init(&frameTimer, frameCb, 0., 1.0 / scene.hz);
    frameTimer.data = this;
    ev_timer_start(loop, &frameTimer);

    ev_signal_init(&sigInt, signalCb, SIGINT);
    ev_signal_start(loop, &sigInt);
    ev_signal_init(&sigTerm, signalCb, SIGTERM);
    ev_signal_start(loop, &sigTerm);
    watchersStarted = true;

    sysO << "imway: socket "_sv << cfg.socketName << ", output "_sv << scene.outW << "x"_sv
         << scene.outH << "@"_sv << (i64)scene.hz << endL;
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
    seatCreateGlobal(display, *seat);
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
    xdgPopupDismiss(*(PopupImpl*)&p);
}

void ServerImpl::onFrameTick() {
    // lavapipe — это CPU: без изменений кадр не рисуем вовсе
    if (scene.needsFrame) {
        settleFrames = 3; // ImGui дорисует hover/анимации
    }

    bool active = scene.needsFrame || settleFrames > 0;

    scene.needsFrame = false;

    if (active) {
        settleFrames--;

        // загрузить свежие пиксели в текстуры (субповерхности тоже — у каждой своя)
        for (Surface* s : scene.surfaces) {
            auto* si = (SurfaceImpl*)s;

            if (si->dirty && si->hasContent) {
                if (si->dmabuf) {
                    renderer->importDmabuf(*si);
                } else {
                    renderer->uploadSurface(*si);
                }

                si->dirty = false;
            }
        }

        renderer->renderFrame(scene);

        if (kms) {
            kms->present(renderer->readbackData());
        }

        // frame callbacks — всем деревьям, показанным в кадре (попапам тоже,
        // GTK не рисует контент меню, пока не получит frame done)
        u32 t = nowMsec();

        for (Toplevel* tl : scene.toplevels) {
            if (tl->mapped && tl->surface) {
                fireFrameCallbacks(*(SurfaceImpl*)tl->surface, t);
            }
        }

        for (Popup* p : scene.popups) {
            if (p->surface) {
                fireFrameCallbacks(*(SurfaceImpl*)p->surface, t);
            }
        }

        // ресайз ImGui-окном: контент-регион разошёлся с размером поверхности
        for (Toplevel* tl : scene.toplevels) {
            auto* ti = (ToplevelImpl*)tl;

            if (!ti->mapped || !ti->surface || ti->desiredW <= 0) {
                continue;
            }

            bool differsView =
                ti->desiredW != ti->surface->viewW() || ti->desiredH != ti->surface->viewH();
            bool differsSent = ti->desiredW != ti->cfgW || ti->desiredH != ti->cfgH;

            if (differsView && differsSent) {
                xdgToplevelConfigureSize(*ti, ti->desiredW, ti->desiredH);
            }
        }
    }

    scene.framesDone++;

    if (cfg.framesLimit > 0 && scene.framesDone >= cfg.framesLimit) {
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

u32 nowMsec() {
    timespec ts{};

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (u32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
