// Модель композитора (поверхности/окна) и Server — ядро: event loop,
// все wayland-протоколы. Реализация целиком в server.cpp.
#pragma once

#include <wayland-server-core.h>

#include <std/lib/vector.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Control;
struct InputLinux;
struct Kms;
struct Popup;
struct Renderer;
struct Seat;
struct Server;
struct Subsurface;
struct SurfaceTexture;
struct Toplevel;
struct XdgSurface;

struct RectI {
    i32 x = 0, y = 0, w = 0, h = 0;
};

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

struct Surface {
    Server* server = nullptr;
    wl_resource* res = nullptr;

    // pending-состояние (double buffering протокола)
    struct {
        wl_resource* buffer = nullptr;
        bool newlyAttached = false;
        wl_listener bufferDestroy{};
        bool bufferDestroyArmed = false;
        stl::Vector<wl_resource*> frames;
        bool inputRegionSet = false; // false = вся поверхность
        stl::Vector<RectI> inputRegion;
    } pending;

    // input region (текущий): куда поверхность принимает указатель
    bool inputRegionSet = false;
    stl::Vector<RectI> inputRegion;
    bool inputContains(double sx, double sy) const;

    // текущее состояние
    int width = 0, height = 0;
    stl::Vector<u8> pixels; // BGRA, плотные строки w*4 (только shm-путь)
    bool dirty = false;     // контент изменился с последней загрузки в текстуру
    bool hasContent = false;
    stl::Vector<wl_resource*> frameCbs;

    // dmabuf-контент: буфер держим до замены (рендер читает память напрямую)
    wl_resource* dmabufBuffer = nullptr;
    wl_listener dmabufDestroy{};
    bool dmabufDestroyArmed = false;

    SurfaceTexture* texture = nullptr; // владеет Renderer

    // wp_viewport: кроп (координаты буфера) и масштаб (размер поверхности)
    struct {
        wl_resource* res = nullptr; // живой wp_viewport (максимум один)
        // pending: -1 = unset; применяется на commit
        double pendSx = -1, pendSy = -1, pendSw = -1, pendSh = -1;
        int pendDw = -1, pendDh = -1;
        // текущее
        bool hasSrc = false, hasDst = false;
        double sx = 0, sy = 0, sw = 0, sh = 0;
        int dw = 0, dh = 0;
    } vp;

    // итоговый размер на экране с учётом вьюпорта
    int viewW() const;
    int viewH() const;

    // состояние из последнего ImGui-кадра (для маршрутизации input)
    float imgX = 0, imgY = 0; // экранная позиция Image-итема
    bool hovered = false;

    XdgSurface* xdg = nullptr; // роль xdg_surface
    Subsurface* sub = nullptr; // роль wl_subsurface

    // дети-субповерхности: stackBelow рисуются до этой поверхности, stackAbove — после,
    // обе — по порядку списка (низ → верх)
    stl::Vector<Subsurface*> stackBelow;
    stl::Vector<Subsurface*> stackAbove;

    // корень дерева субповерхностей (сама поверхность, если не суб-)
    Surface* rootSurface();
    // toplevel корня дерева (nullptr для orphan/попапов)
    Toplevel* rootToplevel();
};

struct Subsurface {
    Surface* surface = nullptr; // сама субповерхность
    Surface* parent = nullptr;
    wl_resource* res = nullptr;

    int x = 0, y = 0; // позиция в координатах родителя
    int pendingX = 0, pendingY = 0;
    bool pendingPos = false; // применяется на commit родителя
    bool sync = true;        // режим по умолчанию — synchronized

    // кэш состояния для sync-коммитов (применяется на commit родителя)
    struct {
        bool valid = false;
        bool hasContent = false;
        int width = 0, height = 0;
        stl::Vector<u8> pixels;
        stl::Vector<wl_resource*> frames;
    } cache;

    bool effectiveSync() const; // sync у себя или у любого предка-субповерхности
};

struct XdgSurface {
    Server* server = nullptr;
    wl_resource* res = nullptr;
    Surface* surface = nullptr;
    Toplevel* toplevel = nullptr;
    Popup* popup = nullptr;
    bool initialConfigureSent = false;
    bool acked = false;
};

struct Toplevel {
    Server* server = nullptr;
    wl_resource* res = nullptr;
    XdgSurface* xdg = nullptr;
    u64 id = 0;
    // фиксированные буферы: клиенты меняют title постоянно, интернить — растить пул
    char title[256] = "(без имени)";
    char appId[128] = "";
    bool mapped = false;

    // ресайз: ImGui-окно → configure клиенту
    bool winSizeSet = false;        // начальный размер ImGui-окна выставлен
    int desiredW = 0, desiredH = 0; // контент-регион из последнего кадра
    int cfgW = 0, cfgH = 0;         // последний отправленный configure
};

struct Popup {
    Server* server = nullptr;
    wl_resource* res = nullptr;
    XdgSurface* xdg = nullptr;
    Surface* parent = nullptr; // поверхность родителя (toplevel или другой попап)
    int x = 0, y = 0;          // позиция относительно родителя (из позиционера)
    int w = 0, h = 0;          // размер из позиционера
    bool mapped = false;
    bool grab = false;
};

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
    // разделяемое состояние: модель и подсистемы, нужные seat/renderer/бэкендам;
    // всё остальное (аллокаторы, ev-вотчеры, конфиг) — приватно в реализации
    wl_display* display = nullptr;
    struct ev_loop* loop = nullptr;

    stl::Vector<Surface*> surfaces;
    stl::Vector<Toplevel*> toplevels;
    stl::Vector<Popup*> popups; // порядок создания = порядок стека

    Renderer* renderer = nullptr;
    Seat* seat = nullptr;

    int outW = 1280, outH = 800; // kms переписывает под режим дисплея
    double hz = 60.0;
    int framesDone = 0;

    // рендер только по необходимости: тик без изменений — пустой (lavapipe = CPU)
    bool needsFrame = true;

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
