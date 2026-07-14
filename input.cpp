#include "input.h"
#include "input_sink.h"
#include "scene.h"
#include "session.h"
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
    int openRestricted(const char* path, int, void* data);
    void closeRestricted(int fd, void* data);

    const libinput_interface liIface = {
        .open_restricted = openRestricted,
        .close_restricted = closeRestricted,
    };

    void inputIoCb(struct ev_loop*, ev_io* w, int);

    struct LibinputSource: public InputSource, public SessionListener {
        struct ev_loop* loop = nullptr;
        Session* session = nullptr;
        InputSink* sink = nullptr;
        Scene* scene = nullptr;
        udev* ud = nullptr;
        libinput* li = nullptr;
        ev_io io{};
        double relX = 0, relY = 0;

        LibinputSource(struct ev_loop* evLoop, Session& ses, InputSink& s, Scene& scn);
        ~LibinputSource() noexcept;

        void sessionEnabled() override {
            libinput_resume(li);
        }

        void sessionDisabled() override {
            libinput_suspend(li);
        }

        void clampCursor() {
            double x0 = 0, y0 = 0, x1 = scene->outW - 1, y1 = scene->outH - 1;

            if (scene->pointerConfined) {
                x0 = scene->confineX0 > x0 ? scene->confineX0 : x0;
                y0 = scene->confineY0 > y0 ? scene->confineY0 : y0;
                x1 = scene->confineX1 < x1 ? scene->confineX1 : x1;
                y1 = scene->confineY1 < y1 ? scene->confineY1 : y1;
            }

            relX = relX < x0 ? x0 : relX > x1 ? x1 : relX;
            relY = relY < y0 ? y0 : relY > y1 ? y1 : relY;
        }

        void dispatch();
    };

    int openRestricted(const char* path, int, void* data) {
        return ((LibinputSource*)data)->session->openDevice(path);
    }

    void closeRestricted(int fd, void* data) {
        ((LibinputSource*)data)->session->closeDevice(fd);
    }

    void inputIoCb(struct ev_loop*, ev_io* w, int) {
        ((LibinputSource*)w->data)->dispatch();
    }

    int drainDeviceAdded(libinput* li) {
        libinput_dispatch(li);

        int n = 0;

        while (libinput_event* ev = libinput_get_event(li)) {
            if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED) {
                n++;
            }

            libinput_event_destroy(ev);
        }

        return n;
    }
}

LibinputSource::LibinputSource(struct ev_loop* evLoop, Session& ses, InputSink& s, Scene& scn) : loop(evLoop), session(&ses), sink(&s), scene(&scn), relX(scn.outW / 2.0), relY(scn.outH / 2.0) {
    ud = udev_new();
    li = libinput_udev_create_context(&liIface, this, ud);
    STD_VERIFY(li);

    STD_VERIFY(libinput_udev_assign_seat(li, ses.seatName()) == 0);

    int devices = drainDeviceAdded(li);

    if (devices == 0) {
        // empty udev db (no udevd running): enumeration finds nothing,
        // open /dev/input/event* directly; no input hotplug in this mode
        libinput_unref(li);
        li = libinput_path_create_context(&liIface, this);
        STD_VERIFY(li);

        for (int i = 0; i < 64; i++) {
            CStr<64> p;

            p << "/dev/input/event"_sv << i;

            if (libinput_path_add_device(li, p.cStr())) {
                devices++;
            }
        }
    }

    ses.addListener(this);

    ev_io_init(&io, inputIoCb, libinput_get_fd(li), EV_READ);
    io.data = this;
    ev_io_start(loop, &io);
    dispatch();
    sysO << "imway: libinput ready, "_sv << devices << " devices"_sv << endL;
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
                double dx = libinput_event_pointer_get_dx(p);
                double dy = libinput_event_pointer_get_dy(p);

                sink->relMotion(dx, dy, libinput_event_pointer_get_dx_unaccelerated(p), libinput_event_pointer_get_dy_unaccelerated(p));

                // while a lock is active the visible cursor stays put,
                // the client works off the relative stream above
                if (scene->pointerLocked) {
                    break;
                }

                relX += dx;
                relY += dy;
                clampCursor();
                sink->motion(relX, relY);

                break;
            }
            case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
                auto* p = libinput_event_get_pointer_event(ev);

                if (scene->pointerLocked) {
                    break;
                }

                relX = libinput_event_pointer_get_absolute_x_transformed(p, scene->outW);
                relY = libinput_event_pointer_get_absolute_y_transformed(p, scene->outH);
                clampCursor();
                sink->motion(relX, relY);

                break;
            }
            case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
                sink->swipeBegin((u32)libinput_event_gesture_get_finger_count(libinput_event_get_gesture_event(ev)));
                break;
            case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE: {
                auto* g = libinput_event_get_gesture_event(ev);

                sink->swipeUpdate(libinput_event_gesture_get_dx(g), libinput_event_gesture_get_dy(g));

                break;
            }
            case LIBINPUT_EVENT_GESTURE_SWIPE_END:
                sink->swipeEnd(libinput_event_gesture_get_cancelled(libinput_event_get_gesture_event(ev)) != 0);
                break;
            case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
                sink->pinchBegin((u32)libinput_event_gesture_get_finger_count(libinput_event_get_gesture_event(ev)));
                break;
            case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE: {
                auto* g = libinput_event_get_gesture_event(ev);

                sink->pinchUpdate(libinput_event_gesture_get_dx(g), libinput_event_gesture_get_dy(g), libinput_event_gesture_get_scale(g), libinput_event_gesture_get_angle_delta(g));

                break;
            }
            case LIBINPUT_EVENT_GESTURE_PINCH_END:
                sink->pinchEnd(libinput_event_gesture_get_cancelled(libinput_event_get_gesture_event(ev)) != 0);
                break;
            case LIBINPUT_EVENT_GESTURE_HOLD_BEGIN:
                sink->holdBegin((u32)libinput_event_gesture_get_finger_count(libinput_event_get_gesture_event(ev)));
                break;
            case LIBINPUT_EVENT_GESTURE_HOLD_END:
                sink->holdEnd(libinput_event_gesture_get_cancelled(libinput_event_get_gesture_event(ev)) != 0);
                break;
            case LIBINPUT_EVENT_POINTER_BUTTON: {
                auto* p = libinput_event_get_pointer_event(ev);

                sink->button(libinput_event_pointer_get_button(p), libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED);

                break;
            }
            case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
            case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
            case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS: {
                auto* p = libinput_event_get_pointer_event(ev);

                double dx = 0, dy = 0;

                if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
                    dy = libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL) / 120.0;
                }

                if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
                    dx = libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL) / 120.0;
                }

                if (dx != 0 || dy != 0) {
                    sink->scroll(dx, dy);
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

InputSource* InputSource::createLibinput(ObjPool* pool, struct ev_loop* loop, Session& session, InputSink& sink, Scene& scene) {
    return pool->make<LibinputSource>(loop, session, sink, scene);
}
