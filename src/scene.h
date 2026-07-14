// Сцена — то, что видят renderer и seat: деревья поверхностей с текстурами,
// экранной геометрией и input-состоянием. Наполняется протокольной частью
// (server.cpp); протокольные кишки (pending, кэши, xdg-ресурсы) живут там же
// в impl-наследниках этих структур.
#pragma once

#include <std/lib/vector.h>
#include <std/sys/types.h>

struct wl_resource;

struct Popup;
struct Subsurface;
struct Surface;
struct SurfaceTexture; // непрозрачный, живёт в renderer.cpp
struct Toplevel;

struct RectI {
    i32 x = 0, y = 0, w = 0, h = 0;
};

inline constexpr int kDmabufMaxPlanes = 4;

// содержимое dmabuf wl_buffer — обменный формат протокол → renderer
struct DmabufBuffer {
    i32 width = 0, height = 0;
    u32 format = 0;   // drm fourcc
    u64 modifier = 0; // одинаковый для всех плоскостей
    int nplanes = 0;
    int fds[kDmabufMaxPlanes] = {-1, -1, -1, -1};
    u32 offsets[kDmabufMaxPlanes] = {};
    u32 strides[kDmabufMaxPlanes] = {};
};

struct Surface {
    wl_resource* res = nullptr; // канал к клиенту: seat шлёт сюда события указателя

    // применённый контент
    int width = 0, height = 0;
    bool hasContent = false;
    stl::Vector<u8> pixels;        // BGRA, плотные строки w*4 (shm-путь)
    DmabufBuffer* dmabuf = nullptr; // не-nullptr = контент в dmabuf, pixels пусты

    SurfaceTexture* texture = nullptr; // владеет Renderer

    // применённый wp_viewport: кроп (координаты буфера) и масштаб
    struct {
        bool hasSrc = false, hasDst = false;
        double sx = 0, sy = 0, sw = 0, sh = 0;
        int dw = 0, dh = 0;
    } vp;

    // итоговый размер на экране с учётом вьюпорта
    int viewW() const;
    int viewH() const;

    // input region: куда поверхность принимает указатель
    bool inputRegionSet = false; // false = вся поверхность
    stl::Vector<RectI> inputRegion;
    bool inputContains(double sx, double sy) const;

    // состояние из последнего ImGui-кадра (пишет renderer, читает seat)
    float imgX = 0, imgY = 0; // экранная позиция Image-итема
    bool hovered = false;

    // дерево субповерхностей: stackBelow рисуются до этой поверхности,
    // stackAbove — после, обе — по порядку списка (низ → верх)
    Subsurface* sub = nullptr; // роль wl_subsurface (nullptr = не суб-)
    stl::Vector<Subsurface*> stackBelow;
    stl::Vector<Subsurface*> stackAbove;

    Toplevel* toplevel = nullptr; // роль xdg_toplevel корня (nullptr у суб- и попапов)

    // корень дерева субповерхностей (сама поверхность, если не суб-)
    Surface* rootSurface();
    // toplevel корня дерева (nullptr для orphan/попапов)
    Toplevel* rootToplevel();
};

struct Subsurface {
    Surface* surface = nullptr; // сама субповерхность
    Surface* parent = nullptr;
    int x = 0, y = 0; // позиция в координатах родителя
};

struct Toplevel {
    Surface* surface = nullptr;
    u64 id = 0;
    // фиксированные буферы: клиенты меняют title постоянно, интернить — растить пул
    char title[256] = "(без имени)";
    char appId[128] = "";
    bool mapped = false;

    // ресайз: ImGui-окно ↔ configure клиенту
    bool winSizeSet = false;        // начальный размер ImGui-окна выставлен
    int desiredW = 0, desiredH = 0; // контент-регион из последнего кадра (пишет renderer)
};

struct Popup {
    Surface* surface = nullptr;
    Surface* parent = nullptr; // поверхность родителя (toplevel или другой попап)
    int x = 0, y = 0;          // позиция относительно родителя (из позиционера)
    bool mapped = false;
    bool grab = false;
};

struct Scene {
    stl::Vector<Surface*> surfaces;
    stl::Vector<Toplevel*> toplevels;
    stl::Vector<Popup*> popups; // порядок создания = порядок стека

    int outW = 1280, outH = 800; // размер output (kms переписывает под режим дисплея)
    double hz = 60.0;
    int framesDone = 0;

    // рендер только по необходимости: тик без изменений — пустой (lavapipe = CPU)
    bool needsFrame = true;
};
