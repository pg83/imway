// zwp_linux_dmabuf_v1: dmabuf-буферы клиентов, импортируемые в Vulkan без копий.
#pragma once

#include <std/sys/types.h>

struct wl_resource;

inline constexpr int kDmabufMaxPlanes = 4;

// содержимое wl_buffer, созданного через zwp_linux_buffer_params_v1
struct DmabufBuffer {
    i32 width = 0, height = 0;
    u32 format = 0;   // drm fourcc
    u64 modifier = 0; // одинаковый для всех плоскостей
    int nplanes = 0;
    int fds[kDmabufMaxPlanes] = {-1, -1, -1, -1};
    u32 offsets[kDmabufMaxPlanes] = {};
    u32 strides[kDmabufMaxPlanes] = {};
};

// nullptr, если ресурс — не наш dmabuf wl_buffer
DmabufBuffer* dmabufFromBufferResource(wl_resource*);
