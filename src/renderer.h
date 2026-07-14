// Vulkan headless рендерер: offscreen target + ImGui + текстуры поверхностей.
#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct Server;
struct Surface;
struct SurfaceTexture; // непрозрачный, живёт в renderer.cpp

struct DmabufFormat {
    u32 fourcc = 0;
    u64 modifier = 0;
};

struct Renderer {
    virtual ~Renderer() noexcept;

    // скопировать пиксели поверхности в staging (пересоздать текстуру при ресайзе)
    virtual void uploadSurface(Surface&) = 0;
    virtual void destroyTexture(SurfaceTexture*) = 0;

    // dmabuf: форматы для рекламы клиентам и прямой импорт в VkImage
    virtual size_t dmabufFormatCount() const = 0;
    virtual DmabufFormat dmabufFormat(size_t i) const = 0;
    virtual bool dmabufFormatSupported(u32 fourcc, u64 modifier) const = 0;
    virtual bool importDmabuf(Surface&) = 0;

    // построить ImGui-кадр по toplevel'ам сервера, записать и исполнить command buffer
    virtual void renderFrame(Server&) = 0;

    virtual bool screenshot(const char* path) = 0;

    // пиксели последнего кадра (BGRA, плотные строки) — источник для scanout
    virtual const void* readbackData() const = 0;

    // бросает stl::Exception, если Vulkan не поднялся
    static Renderer* create(stl::ObjPool* pool, int width, int height);
};
