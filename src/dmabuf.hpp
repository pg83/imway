// zwp_linux_dmabuf_v1: dmabuf-буферы клиентов, импортируемые в Vulkan без копий.
#pragma once

#include <cstdint>

#include <wayland-server-core.h>

struct Server;

inline constexpr int kDmabufMaxPlanes = 4;

// содержимое wl_buffer, созданного через zwp_linux_buffer_params_v1
struct DmabufBuffer {
    int32_t width = 0, height = 0;
    uint32_t format = 0;   // drm fourcc
    uint64_t modifier = 0; // одинаковый для всех плоскостей
    int nplanes = 0;
    int fds[kDmabufMaxPlanes] = {-1, -1, -1, -1};
    uint32_t offsets[kDmabufMaxPlanes] = {};
    uint32_t strides[kDmabufMaxPlanes] = {};
};

// nullptr, если ресурс — не наш dmabuf wl_buffer
DmabufBuffer* dmabuf_from_buffer_resource(wl_resource*);

void linux_dmabuf_create_global(Server&);
