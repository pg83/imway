#include "server.hpp"

#include <cstdio>
#include <ctime>

#include <wayland-server-protocol.h>

#include "dmabuf.hpp"
#include "kms.hpp"
#include "renderer.hpp"
#include "seat.hpp"

#include <imgui.h>

uint32_t now_msec() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void wl_io_cb(struct ev_loop*, ev_io* w, int) {
    auto* s = (Server*)w->data;
    wl_event_loop_dispatch(s->wl_loop, 0);
}

// Инвариант libwayland: не засыпать с несброшенными буферами клиентов.
static void flush_cb(struct ev_loop*, ev_prepare* w, int) {
    auto* s = (Server*)w->data;
    wl_display_flush_clients(s->display);
}

static void frame_cb(struct ev_loop*, ev_timer* w, int) {
    ((Server*)w->data)->on_frame_tick();
}

static void signal_cb(struct ev_loop* loop, ev_signal*, int) {
    ev_break(loop, EVBREAK_ALL);
}

bool Server::init() {
    display = wl_display_create();
    if (!display) return false;
    wl_loop = wl_display_get_event_loop(display);
    loop = ev_default_loop(0);

    if (wl_display_add_socket(display, socket_name.c_str()) != 0) {
        std::fprintf(stderr, "не удалось создать сокет %s (XDG_RUNTIME_DIR?)\n",
                     socket_name.c_str());
        return false;
    }
    wl_display_init_shm(display);

    // kms до рендерера: размер output диктует режим дисплея
    if (backend == "kms") {
        kms = kms_create(*this, drm_device.c_str());
        if (!kms) return false;
    }

    renderer = new Renderer();
    if (!renderer->init(out_w, out_h)) return false;

    seat = new Seat();
    if (!seat->init(*this)) return false;

    if (backend == "kms") {
        if (!kms_start(kms)) return false;
        input = input_linux_create(*this);
        if (!input) std::fprintf(stderr, "imway: без input (продолжаю, но мышь мертва)\n");
        ImGui::GetIO().MouseDrawCursor = true; // композитный курсор
    }

    compositor_create_globals(*this);
    xdg_shell_create_global(*this);
    output_create_global(*this);
    seat_create_global(*this);
    data_device_create_global(*this);
    xdg_decoration_create_global(*this);
    viewporter_create_global(*this);
    linux_dmabuf_create_global(*this);

    if (!control_path.empty()) {
        control = control_create(*this, control_path.c_str());
        if (!control) return false;
    }

    ev_io_init(&wl_io, wl_io_cb, wl_event_loop_get_fd(wl_loop), EV_READ);
    wl_io.data = this;
    ev_io_start(loop, &wl_io);

    ev_prepare_init(&flush_prepare, flush_cb);
    flush_prepare.data = this;
    ev_prepare_start(loop, &flush_prepare);

    ev_timer_init(&frame_timer, frame_cb, 0., 1.0 / hz);
    frame_timer.data = this;
    ev_timer_start(loop, &frame_timer);

    ev_signal_init(&sig_int, signal_cb, SIGINT);
    ev_signal_start(loop, &sig_int);
    ev_signal_init(&sig_term, signal_cb, SIGTERM);
    ev_signal_start(loop, &sig_term);

    std::printf("imway: сокет %s, output %dx%d@%g\n", socket_name.c_str(), out_w, out_h, hz);
    return true;
}

static void fire_frame_callbacks(Surface& s, uint32_t t) {
    // деструктор ресурса удаляет callback из frame_cbs — забираем список до итерации
    auto cbs = std::move(s.frame_cbs);
    s.frame_cbs.clear();
    for (wl_resource* cb : cbs) {
        wl_callback_send_done(cb, t);
        wl_resource_destroy(cb);
    }
    for (Subsurface* c : s.stack_below)
        if (c->surface) fire_frame_callbacks(*c->surface, t);
    for (Subsurface* c : s.stack_above)
        if (c->surface) fire_frame_callbacks(*c->surface, t);
}

void Server::on_frame_tick() {
    // загрузить свежие пиксели в текстуры (субповерхности тоже — у каждой своя)
    for (Surface* s : surfaces)
        if (s->dirty && s->has_content) {
            if (s->dmabuf_buffer)
                renderer->import_dmabuf(*s);
            else
                renderer->upload_surface(*s);
            s->dirty = false;
        }

    renderer->render_frame(*this);
    if (kms) kms_present(kms, renderer->readback_data());

    // frame callbacks — всем деревьям, показанным в кадре
    uint32_t t = now_msec();
    for (Toplevel* tl : toplevels) {
        Surface* surf = tl->xdg ? tl->xdg->surface : nullptr;
        if (tl->mapped && surf) fire_frame_callbacks(*surf, t);
    }

    frames_done++;
    if (frames_limit > 0 && frames_done >= frames_limit) ev_break(loop, EVBREAK_ALL);
}

void Server::run() {
    ev_run(loop, 0);
}

void Server::finish() {
    // скриншот последнего кадра — до разрушения рендерера
    if (!screenshot_path.empty() && renderer) {
        renderer->screenshot(screenshot_path.c_str());
        std::printf("скриншот: %s\n", screenshot_path.c_str());
    }
    control_destroy(control);
    control = nullptr;
    input_linux_destroy(input);
    input = nullptr;
    kms_destroy(kms);
    kms = nullptr;
    // сначала клиенты: их деструкторы освобождают текстуры через renderer
    if (display) wl_display_destroy_clients(display);
    if (seat) {
        seat->finish();
        delete seat;
        seat = nullptr;
    }
    if (renderer) {
        renderer->shutdown();
        delete renderer;
        renderer = nullptr;
    }
    if (display) {
        wl_display_destroy(display);
        display = nullptr;
    }
}
