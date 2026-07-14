// Server — ядро композитора: event loop и все wayland-протоколы.
// Реализация (включая протокольные impl-части модели сцены) целиком в server.cpp.
#pragma once

#include "scene.h"

namespace stl {
    class ObjPool;
}

struct wl_display;

struct ServerConfig {
    const char* backend = "headless"; // headless | kms
    const char* drmDevice = "/dev/dri/card0";
    const char* socketName = "imway-0";
    int outW = 1280, outH = 800;
    double hz = 60.0;
    int framesLimit = 0; // 0 = бесконечно
    const char* screenshotPath = nullptr;
    const char* controlPath = nullptr; // FIFO для инъекции input/команд
};

struct Server {
    wl_display* display = nullptr; // seat берёт отсюда сериалы событий
    Scene scene;

    virtual ~Server() noexcept;

    // цикл до quit/сигнала; на выходе аккуратно гасит клиентов и display
    virtual void run() = 0;
    // закрыть попап (popup_done + unmap); клиент затем уничтожит ресурс
    virtual void dismissPopup(Popup&) = 0;

    // поднимает все глобалы и бэкенды; бросает stl::Exception при фатальных проблемах
    static Server* create(stl::ObjPool* pool, const ServerConfig&);
};

// utils
u32 nowMsec();
