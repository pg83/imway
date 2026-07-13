// zwp_linux_dmabuf_v1 (v3): приём dmabuf-буферов от клиентов.
// Форматы/модификаторы берём у Vulkan-девайса (renderer->dmabuf_formats()).

#include "dmabuf.hpp"

#include <cstdio>
#include <unistd.h>

#include <linux-dmabuf-v1-server-protocol.h>
#include <wayland-server-protocol.h>

#include "renderer.hpp"
#include "server.hpp"

namespace {

void res_destroy(wl_client*, wl_resource* res) { wl_resource_destroy(res); }

// --- wl_buffer из dmabuf ---

const struct wl_buffer_interface dmabuf_wl_buffer_impl = {.destroy = res_destroy};

void dmabuf_buffer_resource_destroyed(wl_resource* res) {
    auto* b = (DmabufBuffer*)wl_resource_get_user_data(res);
    for (int i = 0; i < b->nplanes; i++)
        if (b->fds[i] >= 0) close(b->fds[i]);
    delete b;
}

// --- zwp_linux_buffer_params_v1 ---

struct Params {
    Server* server = nullptr;
    DmabufBuffer* pending = nullptr; // накапливаем add(); nullptr после create
};

Params* params_from(wl_resource* res) { return (Params*)wl_resource_get_user_data(res); }

void params_destroy_resource(wl_resource* res) {
    Params* p = params_from(res);
    if (p->pending) {
        for (int i = 0; i < kDmabufMaxPlanes; i++)
            if (p->pending->fds[i] >= 0) close(p->pending->fds[i]);
        delete p->pending;
    }
    delete p;
}

void params_add(wl_client*, wl_resource* res, int32_t fd, uint32_t plane_idx, uint32_t offset,
                uint32_t stride, uint32_t modifier_hi, uint32_t modifier_lo) {
    Params* p = params_from(res);
    if (!p->pending) {
        wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                               "params уже использованы");
        close(fd);
        return;
    }
    if (plane_idx >= (uint32_t)kDmabufMaxPlanes) {
        wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                               "plane_idx %u вне диапазона", plane_idx);
        close(fd);
        return;
    }
    if (p->pending->fds[plane_idx] >= 0) {
        wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                               "plane %u уже задан", plane_idx);
        close(fd);
        return;
    }
    p->pending->fds[plane_idx] = fd;
    p->pending->offsets[plane_idx] = offset;
    p->pending->strides[plane_idx] = stride;
    p->pending->modifier = ((uint64_t)modifier_hi << 32) | modifier_lo;
    if ((int)plane_idx + 1 > p->pending->nplanes) p->pending->nplanes = plane_idx + 1;
}

// общая часть create/create_immed; возвращает wl_buffer или nullptr (протокол-ошибка уже послана)
wl_resource* params_make_buffer(wl_client* client, wl_resource* res, uint32_t buffer_id,
                                int32_t width, int32_t height, uint32_t format) {
    Params* p = params_from(res);
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
    if (p->pending->nplanes == 0 || p->pending->fds[0] < 0) {
        wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                               "нет plane 0");
        return nullptr;
    }
    if (!p->server->renderer->dmabuf_format_supported(format, p->pending->modifier)) {
        wl_resource_post_error(res, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                               "формат 0x%x mod 0x%llx не поддержан", format,
                               (unsigned long long)p->pending->modifier);
        return nullptr;
    }

    wl_resource* bres = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    if (!bres) {
        wl_client_post_no_memory(client);
        return nullptr;
    }
    DmabufBuffer* b = p->pending;
    p->pending = nullptr; // владение ушло в wl_buffer
    b->width = width;
    b->height = height;
    b->format = format;
    wl_resource_set_implementation(bres, &dmabuf_wl_buffer_impl, b,
                                   dmabuf_buffer_resource_destroyed);
    return bres;
}

void params_create(wl_client* client, wl_resource* res, int32_t width, int32_t height,
                   uint32_t format, uint32_t /*flags*/) {
    wl_resource* buf = params_make_buffer(client, res, 0, width, height, format);
    if (buf)
        zwp_linux_buffer_params_v1_send_created(res, buf);
    else if (!params_from(res)->pending)
        zwp_linux_buffer_params_v1_send_failed(res);
}

void params_create_immed(wl_client* client, wl_resource* res, uint32_t buffer_id, int32_t width,
                         int32_t height, uint32_t format, uint32_t /*flags*/) {
    params_make_buffer(client, res, buffer_id, width, height, format);
}

const struct zwp_linux_buffer_params_v1_interface params_impl = {
    .destroy = res_destroy,
    .add = params_add,
    .create = params_create,
    .create_immed = params_create_immed,
};

// --- zwp_linux_dmabuf_v1 ---

void dmabuf_create_params(wl_client* client, wl_resource* res, uint32_t id) {
    auto* server = (Server*)wl_resource_get_user_data(res);
    wl_resource* pres = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                                           wl_resource_get_version(res), id);
    if (!pres) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* p = new Params();
    p->server = server;
    p->pending = new DmabufBuffer();
    wl_resource_set_implementation(pres, &params_impl, p, params_destroy_resource);
}

void dmabuf_get_default_feedback(wl_client*, wl_resource* res, uint32_t) {
    wl_resource_post_error(res, WL_DISPLAY_ERROR_IMPLEMENTATION,
                           "feedback (v4) не реализован");
}

void dmabuf_get_surface_feedback(wl_client*, wl_resource* res, uint32_t, wl_resource*) {
    wl_resource_post_error(res, WL_DISPLAY_ERROR_IMPLEMENTATION,
                           "feedback (v4) не реализован");
}

const struct zwp_linux_dmabuf_v1_interface dmabuf_impl = {
    .destroy = res_destroy,
    .create_params = dmabuf_create_params,
    .get_default_feedback = dmabuf_get_default_feedback,
    .get_surface_feedback = dmabuf_get_surface_feedback,
};

void dmabuf_bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
    auto* server = (Server*)data;
    wl_resource* res = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    if (!res) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &dmabuf_impl, server, nullptr);

    // v1: format-события; v3: modifier-события
    for (const auto& fm : server->renderer->dmabuf_formats()) {
        if (version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION)
            zwp_linux_dmabuf_v1_send_modifier(res, fm.fourcc, (uint32_t)(fm.modifier >> 32),
                                              (uint32_t)(fm.modifier & 0xffffffff));
        else
            zwp_linux_dmabuf_v1_send_format(res, fm.fourcc);
    }
}

} // namespace

DmabufBuffer* dmabuf_from_buffer_resource(wl_resource* res) {
    if (!wl_resource_instance_of(res, &wl_buffer_interface, &dmabuf_wl_buffer_impl))
        return nullptr;
    return (DmabufBuffer*)wl_resource_get_user_data(res);
}

void linux_dmabuf_create_global(Server& server) {
    if (server.renderer->dmabuf_formats().empty()) {
        std::fprintf(stderr, "imway: vulkan без dmabuf-импорта — глобал linux_dmabuf не создан\n");
        return;
    }
    wl_global_create(server.display, &zwp_linux_dmabuf_v1_interface, 3, &server, dmabuf_bind);
}
