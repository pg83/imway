// wl_compositor + wl_surface + wl_region (минимум для M1).

#include <cstdio>
#include <cstring>

#include <wayland-server-protocol.h>

#include "renderer.hpp"
#include "server.hpp"

namespace {

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

void copy_shm_buffer(Surface& s, wl_shm_buffer* shm) {
    int32_t w = wl_shm_buffer_get_width(shm);
    int32_t h = wl_shm_buffer_get_height(shm);
    int32_t stride = wl_shm_buffer_get_stride(shm);
    uint32_t fmt = wl_shm_buffer_get_format(shm);
    if (fmt != WL_SHM_FORMAT_ARGB8888 && fmt != WL_SHM_FORMAT_XRGB8888) {
        std::fprintf(stderr, "imway: неподдержанный shm-формат 0x%x\n", fmt);
        return;
    }
    s.width = w;
    s.height = h;
    s.pixels.resize((size_t)w * h * 4);
    wl_shm_buffer_begin_access(shm);
    auto* src = (const uint8_t*)wl_shm_buffer_get_data(shm);
    for (int32_t y = 0; y < h; y++)
        std::memcpy(s.pixels.data() + (size_t)y * w * 4, src + (size_t)y * stride, (size_t)w * 4);
    wl_shm_buffer_end_access(shm);
    s.dirty = true;
    s.has_content = true;
}

void surface_commit(wl_client*, wl_resource* res) {
    Surface& s = *surface_from(res);

    if (s.pending.newly_attached) {
        if (!s.pending.buffer) {
            s.has_content = false;
            s.width = s.height = 0;
        } else if (wl_shm_buffer* shm = wl_shm_buffer_get(s.pending.buffer)) {
            copy_shm_buffer(s, shm);
            // копия снята — буфер можно вернуть клиенту сразу
            wl_buffer_send_release(s.pending.buffer);
        } else {
            std::fprintf(stderr, "imway: не-shm буфер, в M1 не поддержан\n");
        }
        detach_pending_buffer(s);
        s.pending.newly_attached = false;
    }

    for (wl_resource* cb : s.pending.frames) s.frame_cbs.push_back(cb);
    s.pending.frames.clear();

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

// --- wl_subcompositor (M1: инертный — субповерхности принимаем, но не рендерим) ---

void subsurface_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }
void subsurface_set_position(wl_client*, wl_resource*, int32_t, int32_t) {}
void subsurface_place_above(wl_client*, wl_resource*, wl_resource*) {}
void subsurface_place_below(wl_client*, wl_resource*, wl_resource*) {}
void subsurface_set_sync(wl_client*, wl_resource*) {}
void subsurface_set_desync(wl_client*, wl_resource*) {}

const struct wl_subsurface_interface subsurface_impl = {
    .destroy = subsurface_destroy,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync,
};

void subcompositor_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

void subcompositor_get_subsurface(wl_client* client, wl_resource* res, uint32_t id,
                                  wl_resource* /*surface*/, wl_resource* /*parent*/) {
    wl_resource* sres =
        wl_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(res), id);
    if (!sres) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(sres, &subsurface_impl, nullptr, nullptr);
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
