// wl_compositor + wl_surface + wl_region + wl_subcompositor.

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <wayland-server-protocol.h>

#include "dmabuf.hpp"
#include "renderer.hpp"
#include "seat.hpp"
#include "server.hpp"

Toplevel* Surface::root_toplevel() {
    Surface* s = this;
    // вверх по цепочке субповерхностей до корня
    while (s->sub) {
        if (!s->sub->parent) return nullptr; // сирота
        s = s->sub->parent;
    }
    return s->xdg ? s->xdg->toplevel : nullptr;
}

bool Subsurface::effective_sync() const {
    for (const Subsurface* s = this; s; s = s->parent ? s->parent->sub : nullptr)
        if (s->sync) return true;
    return false;
}

namespace {

void unlink_from_parent(Subsurface&); // определено ниже, в секции subcompositor

Surface* surface_from(wl_resource* res) {
    return (Surface*)wl_resource_get_user_data(res);
}

void detach_pending_buffer(Surface& s) {
    if (s.pending.buffer_destroy_armed) {
        wl_list_remove(&s.pending.buffer_destroy.link);
        s.pending.buffer_destroy_armed = false;
    }
    s.pending.buffer = nullptr;
}

void pending_buffer_destroyed(wl_listener* l, void*) {
    Surface* s = wl_container_of(l, s, pending.buffer_destroy);
    s->pending.buffer = nullptr;
    s->pending.buffer_destroy_armed = false;
    wl_list_remove(&s->pending.buffer_destroy.link);
}

// --- удержание dmabuf-буфера (рендер читает его память напрямую) ---

void held_dmabuf_destroyed(wl_listener* l, void*) {
    Surface* s = wl_container_of(l, s, dmabuf_destroy);
    // клиент уничтожил буфер, пока тот показан: текстура уже импортирована
    // (память живёт на нашем fd-дубликате), просто забываем ресурс
    s->dmabuf_buffer = nullptr;
    s->dmabuf_destroy_armed = false;
    wl_list_remove(&s->dmabuf_destroy.link);
}

void release_held_dmabuf(Surface& s) {
    if (!s.dmabuf_buffer) return;
    wl_buffer_send_release(s.dmabuf_buffer);
    if (s.dmabuf_destroy_armed) {
        wl_list_remove(&s.dmabuf_destroy.link);
        s.dmabuf_destroy_armed = false;
    }
    s.dmabuf_buffer = nullptr;
}

void hold_dmabuf(Surface& s, wl_resource* buffer) {
    release_held_dmabuf(s);
    s.dmabuf_buffer = buffer;
    s.dmabuf_destroy.notify = held_dmabuf_destroyed;
    wl_resource_add_destroy_listener(buffer, &s.dmabuf_destroy);
    s.dmabuf_destroy_armed = true;
}

// --- wl_surface ---

void surface_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void surface_attach(wl_client*, wl_resource* res, wl_resource* buffer, int32_t, int32_t) {
    Surface& s = *surface_from(res);
    detach_pending_buffer(s);
    s.pending.buffer = buffer;
    s.pending.newly_attached = true;
    if (buffer) {
        s.pending.buffer_destroy.notify = pending_buffer_destroyed;
        wl_resource_add_destroy_listener(buffer, &s.pending.buffer_destroy);
        s.pending.buffer_destroy_armed = true;
    }
}

void surface_damage(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {
    // M1: полная перезаливка на каждый commit, damage учтём позже
}

void frame_callback_destroyed(wl_resource* cb) {
    Surface* s = (Surface*)wl_resource_get_user_data(cb);
    if (!s) return;
    std::erase(s->pending.frames, cb);
    std::erase(s->frame_cbs, cb);
}

void surface_frame(wl_client* client, wl_resource* res, uint32_t id) {
    Surface& s = *surface_from(res);
    wl_resource* cb = wl_resource_create(client, &wl_callback_interface, 1, id);
    if (!cb) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(cb, nullptr, &s, frame_callback_destroyed);
    s.pending.frames.push_back(cb);
}

void surface_set_opaque_region(wl_client*, wl_resource*, wl_resource*) {}
void surface_set_input_region(wl_client*, wl_resource*, wl_resource*) {}

void copy_shm_buffer_to(wl_shm_buffer& shm, int& out_w, int& out_h, std::vector<uint8_t>& out) {
    int32_t w = wl_shm_buffer_get_width(&shm);
    int32_t h = wl_shm_buffer_get_height(&shm);
    int32_t stride = wl_shm_buffer_get_stride(&shm);
    uint32_t fmt = wl_shm_buffer_get_format(&shm);
    if (fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XRGB8888) {
        std::fprintf(stderr, "imway: неподдержанный shm-формат 0x%x\n", fmt);
        out_w = out_h = 0;
        return;
    }
    out_w = w;
    out_h = h;
    out.resize((size_t)w * h * 4);
    wl_shm_buffer_begin_access(&shm);
    auto* src = (const uint8_t*)wl_shm_buffer_get_data(&shm);
    for (int32_t y = 0; y < h; y++)
        std::memcpy(out.data() + (size_t)y * w * 4, src + (size_t)y * stride, (size_t)w * 4);
    wl_shm_buffer_end_access(&shm);
}

void copy_shm_buffer(Surface& s, wl_shm_buffer* shm) {
    copy_shm_buffer_to(*shm, s.width, s.height, s.pixels);
    if (s.width > 0) {
        s.dirty = true;
        s.has_content = true;
    }
}

// применить кэш sync-субповерхности и рекурсивно кэши её sync-детей
void apply_subsurface_cache(Subsurface& sub) {
    if (sub.cache.valid) {
        Surface& s = *sub.surface;
        s.has_content = sub.cache.has_content;
        s.width = sub.cache.width;
        s.height = sub.cache.height;
        if (sub.cache.has_content && !sub.cache.pixels.empty()) {
            s.pixels = std::move(sub.cache.pixels);
            s.dirty = true;
        }
        for (wl_resource* cb : sub.cache.frames) s.frame_cbs.push_back(cb);
        sub.cache.frames.clear();
        sub.cache.pixels.clear();
        sub.cache.valid = false;
    }
    if (sub.pending_pos) {
        sub.x = sub.pending_x;
        sub.y = sub.pending_y;
        sub.pending_pos = false;
    }
    // спека: commit родителя применяет закешированное состояние всего sync-поддерева
    for (Subsurface* c : sub.surface->stack_below)
        if (c->sync) apply_subsurface_cache(*c);
    for (Subsurface* c : sub.surface->stack_above)
        if (c->sync) apply_subsurface_cache(*c);
}

void apply_children_caches(Surface& s) {
    for (auto* stack : {&s.stack_below, &s.stack_above})
        for (Subsurface* c : *stack) {
            // позиция двойнобуферизована коммитом родителя для любых детей
            if (c->pending_pos) {
                c->x = c->pending_x;
                c->y = c->pending_y;
                c->pending_pos = false;
            }
            if (c->sync) apply_subsurface_cache(*c);
        }
}

void surface_commit(wl_client*, wl_resource* res) {
    Surface& s = *surface_from(res);
    s.server->needs_frame = true;
    bool to_cache = s.sub && s.sub->effective_sync();

    if (s.pending.newly_attached) {
        if (!s.pending.buffer) {
            if (to_cache) {
                s.sub->cache.valid = true;
                s.sub->cache.has_content = false;
                s.sub->cache.width = s.sub->cache.height = 0;
                s.sub->cache.pixels.clear();
            } else {
                s.has_content = false;
                s.width = s.height = 0;
            }
        } else if (wl_shm_buffer* shm = wl_shm_buffer_get(s.pending.buffer)) {
            if (to_cache) {
                // снимаем копию сразу (буфер возвращается клиенту), показ — на commit родителя
                copy_shm_buffer_to(*shm, s.sub->cache.width, s.sub->cache.height,
                                   s.sub->cache.pixels);
                s.sub->cache.has_content = s.sub->cache.width > 0;
                s.sub->cache.valid = true;
            } else {
                copy_shm_buffer(s, shm);
            }
            wl_buffer_send_release(s.pending.buffer);
            release_held_dmabuf(s); // на случай смены dmabuf → shm
        } else if (DmabufBuffer* db = dmabuf_from_buffer_resource(s.pending.buffer)) {
            // dmabuf применяем сразу даже для sync-субповерхностей (без кэша):
            // буфер один, копий нет — упрощение, приемлемое для M3
            hold_dmabuf(s, s.pending.buffer);
            s.width = db->width;
            s.height = db->height;
            s.pixels.clear();
            s.has_content = true;
            s.dirty = true;
        } else {
            std::fprintf(stderr, "imway: неизвестный тип буфера\n");
        }
        detach_pending_buffer(s);
        s.pending.newly_attached = false;
    }

    if (to_cache) {
        for (wl_resource* cb : s.pending.frames) s.sub->cache.frames.push_back(cb);
        s.pending.frames.clear();
        return; // остальное (кэши детей) — когда применится наш кэш
    }

    for (wl_resource* cb : s.pending.frames) s.frame_cbs.push_back(cb);
    s.pending.frames.clear();

    viewport_apply_pending(s);

    // desync-субповерхность: позиция всё равно применяется коммитом родителя,
    // но контент — сразу (уже применён выше)
    apply_children_caches(s);

    if (s.xdg) xdg_handle_commit(s);
}

void surface_set_buffer_transform(wl_client*, wl_resource*, int32_t) {}
void surface_set_buffer_scale(wl_client*, wl_resource*, int32_t) {}
void surface_damage_buffer(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
void surface_offset(wl_client*, wl_resource*, int32_t, int32_t) {}

const struct wl_surface_interface surface_impl = {
    .destroy = surface_destroy,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame,
    .set_opaque_region = surface_set_opaque_region,
    .set_input_region = surface_set_input_region,
    .commit = surface_commit,
    .set_buffer_transform = surface_set_buffer_transform,
    .set_buffer_scale = surface_set_buffer_scale,
    .damage_buffer = surface_damage_buffer,
    .offset = surface_offset,
};

void surface_resource_destroyed(wl_resource* res) {
    Surface* s = surface_from(res);
    detach_pending_buffer(*s);
    for (wl_resource* cb : s->pending.frames) wl_resource_set_user_data(cb, nullptr);
    for (wl_resource* cb : s->frame_cbs) wl_resource_set_user_data(cb, nullptr);
    if (s->xdg) s->xdg->surface = nullptr;
    if (s->sub) { // роль-субповерхность: выпасть из стека родителя
        unlink_from_parent(*s->sub);
        s->sub->surface = nullptr;
    }
    for (Subsurface* c : s->stack_below) c->parent = nullptr; // дети-сироты не рендерятся
    for (Subsurface* c : s->stack_above) c->parent = nullptr;
    if (s->server->seat) s->server->seat->surface_gone(s);
    release_held_dmabuf(*s);
    viewport_surface_gone(*s);
    s->server->needs_frame = true;
    if (s->texture && s->server->renderer) s->server->renderer->destroy_texture(s->texture);
    s->server->surfaces.remove(s);
    delete s;
}

// --- wl_region (храним ничего: регионы в M1 не используются) ---

void region_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }
void region_add(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
void region_subtract(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}

const struct wl_region_interface region_impl = {
    .destroy = region_destroy,
    .add = region_add,
    .subtract = region_subtract,
};

// --- wl_compositor ---

void compositor_create_surface(wl_client* client, wl_resource* res, uint32_t id) {
    auto* server = (Server*)wl_resource_get_user_data(res);
    wl_resource* sres =
        wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(res), id);
    if (!sres) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* s = new Surface();
    s->server = server;
    s->res = sres;
    server->surfaces.push_back(s);
    wl_resource_set_implementation(sres, &surface_impl, s, surface_resource_destroyed);
}

void compositor_create_region(wl_client* client, wl_resource* res, uint32_t id) {
    wl_resource* rres =
        wl_resource_create(client, &wl_region_interface, wl_resource_get_version(res), id);
    if (!rres) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(rres, &region_impl, nullptr, nullptr);
}

const struct wl_compositor_interface compositor_impl = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region,
};

void compositor_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    wl_resource* res = wl_resource_create(client, &wl_compositor_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &compositor_impl, data, nullptr);
}

// --- wl_subcompositor / wl_subsurface ---

Subsurface* sub_from(wl_resource* res) { return (Subsurface*)wl_resource_get_user_data(res); }

void unlink_from_parent(Subsurface& sub) {
    if (!sub.parent) return;
    std::erase(sub.parent->stack_below, &sub);
    std::erase(sub.parent->stack_above, &sub);
    sub.parent = nullptr;
}

void subsurface_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void subsurface_set_position(wl_client*, wl_resource* res, int32_t x, int32_t y) {
    Subsurface* sub = sub_from(res);
    if (!sub) return;
    sub->pending_x = x;
    sub->pending_y = y;
    sub->pending_pos = true;
}

// найти позицию sibling'а в стеках родителя; nullptr-стек = ref это сам родитель
void subsurface_restack(Subsurface& sub, Surface* ref_surface, bool above) {
    Surface* parent = sub.parent;
    if (!parent) return;
    std::erase(parent->stack_below, &sub);
    std::erase(parent->stack_above, &sub);

    if (ref_surface == parent) {
        // относительно самого родителя
        if (above)
            parent->stack_above.insert(parent->stack_above.begin(), &sub);
        else
            parent->stack_below.push_back(&sub);
        return;
    }
    Subsurface* ref = ref_surface->sub;
    for (auto* stack : {&parent->stack_below, &parent->stack_above}) {
        auto it = std::find(stack->begin(), stack->end(), ref);
        if (it != stack->end()) {
            stack->insert(above ? it + 1 : it, &sub);
            return;
        }
    }
    // ref не sibling — по спеке ошибка протокола, прощаем и кладём наверх
    parent->stack_above.push_back(&sub);
}

void subsurface_place_above(wl_client*, wl_resource* res, wl_resource* sibling) {
    Subsurface* sub = sub_from(res);
    if (sub && sibling) subsurface_restack(*sub, surface_from(sibling), true);
}

void subsurface_place_below(wl_client*, wl_resource* res, wl_resource* sibling) {
    Subsurface* sub = sub_from(res);
    if (sub && sibling) subsurface_restack(*sub, surface_from(sibling), false);
}

void subsurface_set_sync(wl_client*, wl_resource* res) {
    if (Subsurface* sub = sub_from(res)) sub->sync = true;
}

void subsurface_set_desync(wl_client*, wl_resource* res) {
    Subsurface* sub = sub_from(res);
    if (!sub) return;
    sub->sync = false;
    // переход в desync применяет накопленный кэш
    if (!sub->effective_sync() && sub->cache.valid) apply_subsurface_cache(*sub);
}

const struct wl_subsurface_interface subsurface_impl = {
    .destroy = subsurface_destroy,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync,
};

void subsurface_resource_destroyed(wl_resource* res) {
    Subsurface* sub = sub_from(res);
    if (!sub) return;
    unlink_from_parent(*sub);
    for (wl_resource* cb : sub->cache.frames) wl_resource_destroy(cb);
    if (sub->surface) sub->surface->sub = nullptr;
    delete sub;
}

void subcompositor_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void subcompositor_get_subsurface(wl_client* client, wl_resource* res, uint32_t id,
                                  wl_resource* surface_res, wl_resource* parent_res) {
    Surface* surface = surface_from(surface_res);
    Surface* parent = surface_from(parent_res);
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
    auto* sub = new Subsurface();
    sub->surface = surface;
    sub->parent = parent;
    sub->res = sres;
    surface->sub = sub;
    parent->stack_above.push_back(sub); // новая субповерхность — наверху стека
    wl_resource_set_implementation(sres, &subsurface_impl, sub, subsurface_resource_destroyed);
}

const struct wl_subcompositor_interface subcompositor_impl = {
    .destroy = subcompositor_destroy,
    .get_subsurface = subcompositor_get_subsurface,
};

void subcompositor_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    wl_resource* res = wl_resource_create(client, &wl_subcompositor_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &subcompositor_impl, data, nullptr);
}

} // namespace

void compositor_create_globals(Server& server) {
    wl_global_create(server.display, &wl_compositor_interface, 4, &server, compositor_bind);
    wl_global_create(server.display, &wl_subcompositor_interface, 1, &server, subcompositor_bind);
}
