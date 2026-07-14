// zwp_linux_dmabuf_v1 (v3): приём dmabuf-буферов от клиентов.
// Форматы/модификаторы берём у Vulkan-девайса (renderer->dmabufFormats).

#include "linux_dmabuf.h"
#include "renderer.h"
#include "server.h"
#include "util.h"

#include <unistd.h>

#include <linux-dmabuf-v1-server-protocol.h>
#include <wayland-server-protocol.h>

#include <std/ios/sys.h>

using namespace stl;

namespace {
    void resDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    // --- wl_buffer из dmabuf ---

    const struct wl_buffer_interface dmabufWlBufferImpl = {.destroy = resDestroy};

    struct BufferBox;
    struct Params;

    struct DmabufState { // user data глобала; владеет аллокаторами модуля
        Server* server = nullptr;
        ObjList<BufferBox>* boxes = nullptr;
        ObjList<Params>* params = nullptr;
    };

    // wl_buffer возит (DmabufState*, DmabufBuffer*) — упакуем рядом
    struct BufferBox {
        DmabufState* state = nullptr;
        DmabufBuffer buf;
    };

    void dmabufBufferResourceDestroyed(wl_resource* res) {
        auto* box = (BufferBox*)wl_resource_get_user_data(res);

        for (int i = 0; i < box->buf.nplanes; i++) {
            if (box->buf.fds[i] >= 0) {
                close(box->buf.fds[i]);
            }
        }

        box->state->boxes->release(box);
    }

    // --- zwp_linux_buffer_params_v1 ---

    struct Params {
        DmabufState* state = nullptr;
        BufferBox* pending = nullptr; // накапливаем add(); nullptr после create
    };

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

            p->state->boxes->release(p->pending);
        }

        p->state->params->release(p);
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

        Renderer* renderer = p->state->server->renderer;

        if (!renderer->dmabufFormatSupported(format, b.modifier)) {
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

    // --- zwp_linux_dmabuf_v1 ---

    void dmabufCreateParams(wl_client* client, wl_resource* res, u32 id) {
        auto* state = (DmabufState*)wl_resource_get_user_data(res);
        wl_resource* pres = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                                               wl_resource_get_version(res), id);

        if (!pres) {
            wl_client_post_no_memory(client);

            return;
        }

        auto* p = state->params->make();

        p->state = state;
        p->pending = state->boxes->make();
        p->pending->state = state;
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
        auto* state = (DmabufState*)data;
        wl_resource* res = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &dmabufImpl, state, nullptr);

        // v1: format-события; v3: modifier-события
        Renderer* renderer = state->server->renderer;

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

DmabufBuffer* dmabufFromBufferResource(wl_resource* res) {
    if (!wl_resource_instance_of(res, &wl_buffer_interface, &dmabufWlBufferImpl)) {
        return nullptr;
    }

    return &((BufferBox*)wl_resource_get_user_data(res))->buf;
}

void linuxDmabufCreateGlobal(Server& server) {
    if (server.renderer->dmabufFormatCount() == 0) {
        sysE << "imway: vulkan lacks dmabuf import, linux_dmabuf global not created"_sv << endL;

        return;
    }

    auto* state = server.pool->make<DmabufState>();

    state->server = &server;
    state->boxes = server.pool->make<ObjList<BufferBox>>(server.pool);
    state->params = server.pool->make<ObjList<Params>>(server.pool);

    wl_global_create(server.display, &zwp_linux_dmabuf_v1_interface, 3, state, dmabufBind);
}
