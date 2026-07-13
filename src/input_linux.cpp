// libinput → Seat. Абсолютная мышь (usb-tablet в QEMU), относительная,
// кнопки, колесо, клавиатура (сырые evdev-коды).

#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#include <libinput.h>
#include <libudev.h>

#include "kms.hpp"
#include "seat.hpp"
#include "server.hpp"

struct InputLinux {
    Server* server = nullptr;
    udev* ud = nullptr;
    libinput* li = nullptr;
    ev_io io{};
    double rel_x = 0, rel_y = 0; // накопленная позиция для относительных устройств

    bool init(Server&);
    void finish();
    void dispatch();
};

namespace {

int open_restricted(const char* path, int flags, void*) {
    int fd = open(path, flags | O_CLOEXEC);
    return fd < 0 ? -errno : fd;
}

void close_restricted(int fd, void*) { close(fd); }

const libinput_interface li_iface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

void input_io_cb(struct ev_loop*, ev_io* w, int) { ((InputLinux*)w->data)->dispatch(); }

} // namespace

void InputLinux::dispatch() {
    libinput_dispatch(li);
    Seat& seat = *server->seat;
    libinput_event* ev;
    while ((ev = libinput_get_event(li))) {
        switch (libinput_event_get_type(ev)) {
        case LIBINPUT_EVENT_POINTER_MOTION: {
            auto* p = libinput_event_get_pointer_event(ev);
            rel_x += libinput_event_pointer_get_dx(p);
            rel_y += libinput_event_pointer_get_dy(p);
            if (rel_x < 0) rel_x = 0;
            if (rel_y < 0) rel_y = 0;
            if (rel_x > server->out_w - 1) rel_x = server->out_w - 1;
            if (rel_y > server->out_h - 1) rel_y = server->out_h - 1;
            seat.handle_motion(rel_x, rel_y);
            break;
        }
        case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
            auto* p = libinput_event_get_pointer_event(ev);
            rel_x = libinput_event_pointer_get_absolute_x_transformed(p, server->out_w);
            rel_y = libinput_event_pointer_get_absolute_y_transformed(p, server->out_h);
            seat.handle_motion(rel_x, rel_y);
            break;
        }
        case LIBINPUT_EVENT_POINTER_BUTTON: {
            auto* p = libinput_event_get_pointer_event(ev);
            seat.handle_button(libinput_event_pointer_get_button(p),
                               libinput_event_pointer_get_button_state(p) ==
                                   LIBINPUT_BUTTON_STATE_PRESSED);
            break;
        }
        case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
        case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
        case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS: {
            auto* p = libinput_event_get_pointer_event(ev);
            if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
                double v = libinput_event_pointer_get_scroll_value_v120(
                               p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL) /
                           120.0;
                seat.handle_scroll(v);
            }
            break;
        }
        case LIBINPUT_EVENT_KEYBOARD_KEY: {
            auto* k = libinput_event_get_keyboard_event(ev);
            seat.handle_key(libinput_event_keyboard_get_key(k),
                            libinput_event_keyboard_get_key_state(k) ==
                                LIBINPUT_KEY_STATE_PRESSED);
            break;
        }
        default:
            break;
        }
        libinput_event_destroy(ev);
    }
}

bool InputLinux::init(Server& srv) {
    server = &srv;
    rel_x = srv.out_w / 2.0;
    rel_y = srv.out_h / 2.0;

    ud = udev_new();
    li = libinput_udev_create_context(&li_iface, this, ud);
    if (!li) {
        std::fprintf(stderr, "input: libinput context fail\n");
        return false;
    }
    if (libinput_udev_assign_seat(li, "seat0") != 0) {
        std::fprintf(stderr, "input: assign_seat fail (права на /dev/input?)\n");
        return false;
    }
    ev_io_init(&io, input_io_cb, libinput_get_fd(li), EV_READ);
    io.data = this;
    ev_io_start(server->loop, &io);
    dispatch(); // добавленные устройства
    std::printf("imway: libinput готов\n");
    return true;
}

void InputLinux::finish() {
    if (li) {
        ev_io_stop(server->loop, &io);
        libinput_unref(li);
    }
    if (ud) udev_unref(ud);
}

InputLinux* input_linux_create(Server& server) {
    auto* in = new InputLinux();
    if (!in->init(server)) {
        in->finish();
        delete in;
        return nullptr;
    }
    return in;
}

void input_linux_destroy(InputLinux* in) {
    if (!in) return;
    in->finish();
    delete in;
}
