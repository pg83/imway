// Общие структуры композитора (M1: headless, wl_shm, xdg-toplevel).
#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include <ev.h>
#include <wayland-server-core.h>

struct Renderer;
struct SurfaceTexture;
struct Server;
struct XdgSurface;
struct Subsurface;
struct Toplevel;

struct RectI {
    int32_t x = 0, y = 0, w = 0, h = 0;
};

struct Surface {
    Server* server = nullptr;
    wl_resource* res = nullptr;

    // pending-состояние (double buffering протокола)
    struct {
        wl_resource* buffer = nullptr;
        bool newly_attached = false;
        wl_listener buffer_destroy{};
        bool buffer_destroy_armed = false;
        std::vector<wl_resource*> frames;
        bool input_region_set = false;      // false = вся поверхность
        std::vector<RectI> input_region;
    } pending;

    // input region (текущий): куда поверхность принимает указатель
    bool input_region_set = false;
    std::vector<RectI> input_region;
    bool input_contains(double sx, double sy) const {
        if (!input_region_set) return true;
        for (const RectI& r : input_region)
            if (sx >= r.x && sy >= r.y && sx < r.x + r.w && sy < r.y + r.h) return true;
        return false;
    }

    // текущее состояние
    int width = 0, height = 0;
    std::vector<uint8_t> pixels; // BGRA, плотные строки w*4 (только shm-путь)
    bool dirty = false;          // контент изменился с последней загрузки в текстуру
    bool has_content = false;
    std::vector<wl_resource*> frame_cbs;

    // dmabuf-контент: буфер держим до замены (рендер читает память напрямую)
    wl_resource* dmabuf_buffer = nullptr;
    wl_listener dmabuf_destroy{};
    bool dmabuf_destroy_armed = false;

    SurfaceTexture* texture = nullptr; // владеет Renderer

    // wp_viewport: кроп (координаты буфера) и масштаб (размер поверхности)
    struct {
        wl_resource* res = nullptr; // живой wp_viewport (максимум один)
        // pending: -1 = unset; применяется на commit
        double pend_sx = -1, pend_sy = -1, pend_sw = -1, pend_sh = -1;
        int pend_dw = -1, pend_dh = -1;
        // текущее
        bool has_src = false, has_dst = false;
        double sx = 0, sy = 0, sw = 0, sh = 0;
        int dw = 0, dh = 0;
    } vp;

    // итоговый размер на экране с учётом вьюпорта
    int view_w() const { return vp.has_dst ? vp.dw : vp.has_src ? (int)vp.sw : width; }
    int view_h() const { return vp.has_dst ? vp.dh : vp.has_src ? (int)vp.sh : height; }

    // состояние из последнего ImGui-кадра (для маршрутизации input)
    float img_x = 0, img_y = 0; // экранная позиция Image-итема
    bool hovered = false;

    XdgSurface* xdg = nullptr; // роль xdg_surface
    Subsurface* sub = nullptr; // роль wl_subsurface

    // дети-субповерхности: stack_below рисуются до этой поверхности, stack_above — после,
    // обе — по порядку списка (низ → верх)
    std::vector<Subsurface*> stack_below;
    std::vector<Subsurface*> stack_above;

    // корень дерева субповерхностей (сама поверхность, если не суб-)
    Surface* root_surface();
    // toplevel корня дерева (nullptr для orphan/попапов)
    Toplevel* root_toplevel();
};

struct Subsurface {
    Surface* surface = nullptr; // сама субповерхность
    Surface* parent = nullptr;
    wl_resource* res = nullptr;

    int x = 0, y = 0; // позиция в координатах родителя
    int pending_x = 0, pending_y = 0;
    bool pending_pos = false; // применяется на commit родителя
    bool sync = true;         // режим по умолчанию — synchronized

    // кэш состояния для sync-коммитов (применяется на commit родителя)
    struct {
        bool valid = false;
        bool has_content = false;
        int width = 0, height = 0;
        std::vector<uint8_t> pixels;
        std::vector<wl_resource*> frames;
    } cache;

    bool effective_sync() const; // sync у себя или у любого предка-субповерхности
};

struct XdgSurface {
    Server* server = nullptr;
    wl_resource* res = nullptr;
    Surface* surface = nullptr;
    Toplevel* toplevel = nullptr;
    struct Popup* popup = nullptr;
    bool initial_configure_sent = false;
    bool acked = false;
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
    bool focus_set = false; // ImGui-окно поднято наверх при появлении
};

struct Toplevel {
    Server* server = nullptr;
    wl_resource* res = nullptr;
    XdgSurface* xdg = nullptr;
    uint64_t id = 0;
    std::string title = "(без имени)";
    std::string app_id;
    bool mapped = false;

    // ресайз: ImGui-окно → configure клиенту
    bool win_size_set = false;    // начальный размер ImGui-окна выставлен
    int desired_w = 0, desired_h = 0; // контент-регион из последнего кадра
    int cfg_w = 0, cfg_h = 0;         // последний отправленный configure
};

struct Server {
    wl_display* display = nullptr;
    wl_event_loop* wl_loop = nullptr;
    struct ev_loop* loop = nullptr;

    ev_io wl_io{};
    ev_prepare flush_prepare{};
    ev_timer frame_timer{};
    ev_signal sig_int{}, sig_term{};

    std::list<Surface*> surfaces;
    std::list<Toplevel*> toplevels;
    std::list<Popup*> popups; // порядок создания = порядок стека
    Renderer* renderer = nullptr;
    struct Seat* seat = nullptr;
    struct Control* control = nullptr;
    struct Kms* kms = nullptr;
    struct InputLinux* input = nullptr;
    uint64_t next_toplevel_id = 1;

    // конфигурация
    std::string backend = "headless"; // headless | kms
    std::string drm_device = "/dev/dri/card0";
    std::string socket_name = "imway-0";
    int out_w = 1280, out_h = 800;
    double hz = 60.0;
    int frames_limit = 0; // 0 = бесконечно
    int frames_done = 0;
    std::string screenshot_path;
    std::string control_path; // FIFO для инъекции input/команд

    // рендер только по необходимости: тик без изменений — пустой (lavapipe = CPU)
    bool needs_frame = true;
    int settle_frames = 0; // дорисовать пару кадров после последней активности

    bool init();
    void run();
    void finish();
    void on_frame_tick();
};

// глобалы
void compositor_create_globals(Server&);
void xdg_shell_create_global(Server&);
void output_create_global(Server&);
void data_device_create_global(Server&);
void xdg_decoration_create_global(Server&);
void viewporter_create_global(Server&);

// wp_viewport: применить pending на commit; отвязать при смерти поверхности
void viewport_apply_pending(Surface&);
void viewport_surface_gone(Surface&);

// реакция xdg-роли на commit поверхности (map-логика, configure dance)
void xdg_handle_commit(Surface&);
// послать клиенту configure с новым размером (ресайз ImGui-окном)
void xdg_toplevel_configure_size(Toplevel&, int w, int h);
// закрыть попап (popup_done + unmap); клиент затем уничтожит ресурс
void xdg_popup_dismiss(Popup&);

// control-канал (FIFO)
struct Control* control_create(Server&, const char* path);
void control_destroy(struct Control*);

// utils
uint32_t now_msec();
