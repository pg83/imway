// Wayland state machine — единственный владелец libwayland и xkbcommon:
// wl_display, все глобалы и протоколы, commit-семантика, роли, seat
// (фокус/грабы/клавиатура — это протокольные реакции на вход, не подсистема).
// Потребляет сырой ввод (InputSink), уведомления «кадр показан»
// (FrameListener) и view-фидбек из сцены; мутирует сцену.
#pragma once

#include <stddef.h>

namespace stl {
    class ObjPool;
}

struct DmabufFormat;
struct ev_loop;
struct FrameListener;
struct InputSink;
struct Scene;

struct WaylandConfig {
    const char* socketName = "imway-0";
    // dmabuf-форматы GPU (capability рендера как данные); пусто = без dmabuf
    const DmabufFormat* formats = nullptr;
    size_t formatCount = 0;
};

struct Wayland {
    virtual ~Wayland() noexcept;

    // цикл до quit/сигнала; на выходе аккуратно гасит клиентов и display
    virtual void run() = 0;

    // сырой ввод → протокол (реализация возвращает себя)
    virtual InputSink* sink() = 0;
    // уведомления «кадр показан» → frame callbacks и configure по view-фидбеку
    virtual FrameListener* frameListener() = 0;

    // поднимает сокет и все глобалы; бросает stl::Exception при фатальных проблемах
    static Wayland* create(stl::ObjPool* pool, struct ev_loop* loop, Scene& scene,
                           const WaylandConfig&);
};
