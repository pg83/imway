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
    } pending;

    // текущее состояние
    int width = 0, height = 0;
    std::vector<uint8_t> pixels; // BGRA, плотные строки w*4
    bool dirty = false;          // pixels изменились с последней загрузки в текстуру
    bool has_content = false;
    std::vector<wl_resource*> frame_cbs;

    SurfaceTexture* texture = nullptr; // владеет Renderer

    // состояние из последнего ImGui-кадра (для маршрутизации input)
    float img_x = 0, img_y = 0; // экранная позиция Image-итема
    bool hovered = false;

    XdgSurface* xdg = nullptr; // роль xdg_surface
    Subsurface* sub = nullptr; // роль wl_subsurface

    // дети-субповерхности: stack_below рисуются до этой поверхности, stack_above — после,
    // обе — по порядку списка (низ → верх)
    std::vector<Subsurface*> stack_below;
    std::vector<Subsurface*> stack_above;

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
    bool initial_configure_sent = false;
    bool acked = false;
};

struct Toplevel {
    Server* server = nullptr;
    wl_resource* res = nullptr;
    XdgSurface* xdg = nullptr;
    uint64_t id = 0;
    std::string title = "(без имени)";
    std::string app_id;
    bool mapped = false;
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

// реакция xdg-роли на commit поверхности (map-логика, configure dance)
void xdg_handle_commit(Surface&);

// control-канал (FIFO)
struct Control* control_create(Server&, const char* path);
void control_destroy(struct Control*);

// utils
uint32_t now_msec();
