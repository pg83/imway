#include "input.h"
#include "input_sink.h"
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

    struct LibinputSource: public InputSource {
        struct ev_loop* loop = nullptr;
        InputSink* sink = nullptr;
        int outW = 0, outH = 0;
        udev* ud = nullptr;
        libinput* li = nullptr;
        ev_io io{};
        double relX = 0, relY = 0;

        LibinputSource(struct ev_loop* evLoop, InputSink& s, int w, int h);
        ~LibinputSource() noexcept;

        void dispatch();
    };

    void inputIoCb(struct ev_loop*, ev_io* w, int) {
        ((LibinputSource*)w->data)->dispatch();
    }
}

LibinputSource::LibinputSource(struct ev_loop* evLoop, InputSink& s, int w, int h) : loop(evLoop), sink(&s), outW(w), outH(h), relX(w / 2.0), relY(h / 2.0) {
    ud = udev_new();
    li = libinput_udev_create_context(&liIface, this, ud);
    STD_VERIFY(li);

    STD_VERIFY(libinput_udev_assign_seat(li, "seat0") == 0);

    ev_io_init(&io, inputIoCb, libinput_get_fd(li), EV_READ);
    io.data = this;
    ev_io_start(loop, &io);
    dispatch();
    sysO << "imway: libinput ready"_sv << endL;
}

LibinputSource::~LibinputSource() noexcept {
    if (li) {
        ev_io_stop(loop, &io);
        libinput_unref(li);
    }

    if (ud) {
        udev_unref(ud);
    }
}

void LibinputSource::dispatch() {
    libinput_dispatch(li);

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

                if (relX > outW - 1) {
                    relX = outW - 1;
                }

                if (relY > outH - 1) {
                    relY = outH - 1;
                }

                sink->motion(relX, relY);

                break;
            }
            case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
                auto* p = libinput_event_get_pointer_event(ev);

                relX = libinput_event_pointer_get_absolute_x_transformed(p, outW);
                relY = libinput_event_pointer_get_absolute_y_transformed(p, outH);
                sink->motion(relX, relY);

                break;
            }
            case LIBINPUT_EVENT_POINTER_BUTTON: {
                auto* p = libinput_event_get_pointer_event(ev);

                sink->button(libinput_event_pointer_get_button(p), libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED);

                break;
            }
            case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
            case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
            case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS: {
                auto* p = libinput_event_get_pointer_event(ev);

                if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
                    double v = libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL) /
                               120.0;

                    sink->scroll(v);
                }

                break;
            }
            case LIBINPUT_EVENT_KEYBOARD_KEY: {
                auto* k = libinput_event_get_keyboard_event(ev);

                sink->key(libinput_event_keyboard_get_key(k), libinput_event_keyboard_get_key_state(k) == LIBINPUT_KEY_STATE_PRESSED);

                break;
            }
            default:
                break;
        }

        libinput_event_destroy(ev);
    }
}

InputSource* InputSource::createLibinput(ObjPool* pool, struct ev_loop* loop, InputSink& sink, int outW, int outH) {
    return pool->make<LibinputSource>(loop, sink, outW, outH);
}
