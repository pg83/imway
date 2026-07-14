// libinput → Seat. Абсолютная мышь (usb-tablet в QEMU), относительная,
// кнопки, колесо, клавиатура (сырые evdev-коды).

#include "input_linux.h"
#include "seat.h"
#include "server.h"
#include "util.h"

#include <ev.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <libinput.h>
#include <libudev.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    int openRestricted(const char* path, int flags, void*) {
        int fd = open(path, flags | O_CLOEXEC);

        return fd < 0 ? -errno : fd;
    }

    void closeRestricted(int fd, void*) {
        close(fd);
    }

    const libinput_interface liIface = {
        .open_restricted = openRestricted,
        .close_restricted = closeRestricted,
    };

    void inputIoCb(struct ev_loop*, ev_io* w, int);

    struct InputLinuxImpl: public InputLinux {
        Server* server = nullptr;
        udev* ud = nullptr;
        libinput* li = nullptr;
        ev_io io{};
        double relX = 0, relY = 0; // накопленная позиция для относительных устройств

        InputLinuxImpl(Server& srv);
        ~InputLinuxImpl() noexcept override;

        void dispatch();
    };

    void inputIoCb(struct ev_loop*, ev_io* w, int) {
        ((InputLinuxImpl*)w->data)->dispatch();
    }
}

InputLinuxImpl::InputLinuxImpl(Server& srv)
    : server(&srv)
    , relX(srv.outW / 2.0)
    , relY(srv.outH / 2.0)
{
    ud = udev_new();
    li = libinput_udev_create_context(&liIface, this, ud);
    STD_VERIFY(li); // libinput context не создался

    // права на /dev/input?
    STD_VERIFY(libinput_udev_assign_seat(li, "seat0") == 0);

    ev_io_init(&io, inputIoCb, libinput_get_fd(li), EV_READ);
    io.data = this;
    ev_io_start(server->loop, &io);
    dispatch(); // добавленные устройства
    sysO << "imway: libinput ready"_sv << endL;
}

InputLinuxImpl::~InputLinuxImpl() noexcept {
    if (li) {
        ev_io_stop(server->loop, &io);
        libinput_unref(li);
    }

    if (ud) {
        udev_unref(ud);
    }
}

void InputLinuxImpl::dispatch() {
    libinput_dispatch(li);

    Seat& seat = *server->seat;
    libinput_event* ev;

    while ((ev = libinput_get_event(li))) {
        switch (libinput_event_get_type(ev)) {
            case LIBINPUT_EVENT_POINTER_MOTION: {
                auto* p = libinput_event_get_pointer_event(ev);

                relX += libinput_event_pointer_get_dx(p);
                relY += libinput_event_pointer_get_dy(p);

                if (relX < 0) {
                    relX = 0;
                }

                if (relY < 0) {
                    relY = 0;
                }

                if (relX > server->outW - 1) {
                    relX = server->outW - 1;
                }

                if (relY > server->outH - 1) {
                    relY = server->outH - 1;
                }

                seat.handleMotion(relX, relY);

                break;
            }
            case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
                auto* p = libinput_event_get_pointer_event(ev);

                relX = libinput_event_pointer_get_absolute_x_transformed(p, server->outW);
                relY = libinput_event_pointer_get_absolute_y_transformed(p, server->outH);
                seat.handleMotion(relX, relY);

                break;
            }
            case LIBINPUT_EVENT_POINTER_BUTTON: {
                auto* p = libinput_event_get_pointer_event(ev);

                seat.handleButton(libinput_event_pointer_get_button(p),
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

                    seat.handleScroll(v);
                }

                break;
            }
            case LIBINPUT_EVENT_KEYBOARD_KEY: {
                auto* k = libinput_event_get_keyboard_event(ev);

                seat.handleKey(libinput_event_keyboard_get_key(k),
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

InputLinux::~InputLinux() noexcept {
}

InputLinux* InputLinux::create(ObjPool* pool, Server& server) {
    return pool->make<InputLinuxImpl>(server);
}
