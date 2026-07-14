// Renderer — view сцены: ImGui+Vulkan, ноль знаний о Wayland. Сам владеет
// кадровым клоком: по needsFrame рендерит сцену, презентит в Output и
// уведомляет FrameListener. Сам тянет контент нод по dirty (shm/dmabuf),
// сам владеет текстурами. Реализует InputSink — ImGui и есть оконный
// менеджер, ему нужен сырой ввод.
#pragma once

#include "input.h"

namespace stl {
    class ObjPool;
}

struct DmabufFormat;
struct ev_loop;
struct Output;
struct Scene;

struct FrameListener {
    // кадр показан; msec — таймстемп для frame callbacks
    virtual void frameShown(u32 msec) = 0;
};

struct Renderer: public InputSink {
    virtual ~Renderer() noexcept;

    // dmabuf-форматы, которые умеет GPU (снимаются как данные для wayland-SM)
    virtual size_t dmabufFormatCount() const = 0;
    virtual DmabufFormat dmabufFormat(size_t i) const = 0;

    // уведомления «кадр показан» (подключается после создания SM)
    virtual void setFrameListener(FrameListener*) = 0;

    // PPM последнего показанного кадра
    virtual bool screenshot(const char* path) = 0;

    // framesLimit > 0 — остановить loop после N кадров (тестовый харнесс);
    // бросает stl::Exception, если Vulkan не поднялся
    static Renderer* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene,
                            Output& output, int framesLimit);
};
