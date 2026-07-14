// wl_compositor + wl_surface + wl_region + wl_subcompositor.

#include "linux_dmabuf.h"
#include "renderer.h"
#include "seat.h"
#include "server.h"
#include "util.h"

#include <string.h>

#include <wayland-server-protocol.h>

#include <std/ios/sys.h>

using namespace stl;

namespace {
    void unlinkFromParent(Subsurface&); // определено ниже, в секции subcompositor

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

    // --- wl_surface ---

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

    // регионы: user_data ресурса — RegionBox с прямоугольниками
    struct RegionBox;

    struct CompositorState {
        Server* server = nullptr;
        ObjList<RegionBox>* regions = nullptr;
    };

    struct RegionBox {
        Vector<RectI> rects;
        CompositorState* state = nullptr; // для возврата в ObjList при destroy
    };

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
        server->surfaceAlloc->release(s);
    }

    // --- wl_region ---

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

        box->state->regions->release(box);
    }

    // --- wl_compositor ---

    void compositorCreateSurface(wl_client* client, wl_resource* res, u32 id) {
        auto* state = (CompositorState*)wl_resource_get_user_data(res);
        wl_resource* sres =
            wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(res), id);

        if (!sres) {
            wl_client_post_no_memory(client);

            return;
        }

        Server* server = state->server;
        auto* s = server->surfaceAlloc->make();

        s->server = server;
        s->res = sres;
        server->surfaces.pushBack(s);
        wl_resource_set_implementation(sres, &surfaceImpl, s, surfaceResourceDestroyed);
    }

    void compositorCreateRegion(wl_client* client, wl_resource* res, u32 id) {
        auto* state = (CompositorState*)wl_resource_get_user_data(res);
        wl_resource* rres =
            wl_resource_create(client, &wl_region_interface, wl_resource_get_version(res), id);

        if (!rres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* box = state->regions->make();

        box->state = state;
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

    // --- wl_subcompositor / wl_subsurface ---

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
            server->subsurfaceAlloc->release(sub);
        }
    }

    void subcompositorDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    void subcompositorGetSubsurface(wl_client* client, wl_resource* res, u32 id,
                                    wl_resource* surfaceRes, wl_resource* parentRes) {
        auto* server = (Server*)wl_resource_get_user_data(res);
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

        auto* sub = server->subsurfaceAlloc->make();

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
}

void compositorCreateGlobals(Server& server) {
    auto* state = server.pool->make<CompositorState>();

    state->server = &server;
    state->regions = server.pool->make<ObjList<RegionBox>>(server.pool);

    wl_global_create(server.display, &wl_compositor_interface, 4, state, compositorBind);
    wl_global_create(server.display, &wl_subcompositor_interface, 1, &server, subcompositorBind);
}
