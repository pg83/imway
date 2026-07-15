#include "input.h"
#include "input_sink.h"
#include "scene.h"
#include "session.h"
#include "util.h"

#include <ev.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <libinput.h>

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
        libinput* li = nullptr;
        ev_io io{};
        double relX = 0, relY = 0;

        // hotplug: inotify on /dev/input, one bit + device slot per eventN
        int inoFd = -1;
        ev_io inoIo{};
        u64 pathBits = 0;
        libinput_device* pathDevs[64] = {};

        LibinputSource(struct ev_loop* evLoop, Session& ses, InputSink& s, Scene& scn);
        ~LibinputSource() noexcept;

        bool pathAdd(int n) {
            if (n < 0 || n >= 64 || (pathBits & (1ull << n))) {
                return false;
            }

            auto& p = sb();

            p << "/dev/input/event"_sv << n;

            if (!libinput_path_add_device(li, p.cStr())) {
                return false;
            }

            pathBits |= 1ull << n;

            return true;
        }

        // the node vanished: yank the device now — libinput only notices a
        // dead fd on its next read, and a replug reusing the same number
        // must not find the slot still taken
        void pathDrop(int n) {
            if (n < 0 || n >= 64) {
                return;
            }

            if (pathDevs[n]) {
                libinput_path_remove_device(pathDevs[n]);
                libinput_device_unref(pathDevs[n]);
                pathDevs[n] = nullptr;
            }

            pathBits &= ~(1ull << n);
        }

        static int sysnameIndex(libinput_device* dev) {
            StringView sys(libinput_device_get_sysname(dev));

            if (!sys.startsWith("event"_sv)) {
                return -1;
            }

            int idx = (int)StringView(sys.begin() + 5, sys.end()).stou();

            return idx >= 0 && idx < 64 ? idx : -1;
        }

        void inotifyEvents();

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

    void inotifyCb(struct ev_loop*, ev_io* w, int) {
        ((LibinputSource*)w->data)->inotifyEvents();
    }

    // libinput ships touchpads with tap-to-click off, the compositor opts in
    void configureDevice(libinput_device* dev) {
        if (libinput_device_config_tap_get_finger_count(dev) > 0) {
            libinput_device_config_tap_set_enabled(dev, LIBINPUT_CONFIG_TAP_ENABLED);
        }
    }
}

// path backend only: the udev one needs a running udevd for enumeration AND
// hotplug; a direct /dev/input scan plus inotify behaves the same either way
LibinputSource::LibinputSource(struct ev_loop* evLoop, Session& ses, InputSink& s, Scene& scn)
    : loop(evLoop)
    , session(&ses)
    , sink(&s)
    , scene(&scn)
    , relX(scn.outW / 2.0)
    , relY(scn.outH / 2.0)
{
    li = libinput_path_create_context(&liIface, this);
    STD_VERIFY(li);

    int devices = 0;

    for (int i = 0; i < 64; i++) {
        if (pathAdd(i)) {
            devices++;
        }
    }

    inoFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    if (inoFd >= 0 && inotify_add_watch(inoFd, "/dev/input", IN_CREATE | IN_ATTRIB | IN_DELETE) >= 0) {
        ev_io_init(&inoIo, inotifyCb, inoFd, EV_READ);
        inoIo.data = this;
        ev_io_start(loop, &inoIo);
    }

    ses.addListener(this);

    ev_io_init(&io, inputIoCb, libinput_get_fd(li), EV_READ);
    io.data = this;
    ev_io_start(loop, &io);
    dispatch();
    sysO << "imway: libinput ready, "_sv << devices << " devices"_sv << endL;
}

void LibinputSource::inotifyEvents() {
    alignas(8) char buf[4096];

    for (;;) {
        ssize_t n = read(inoFd, buf, sizeof(buf));

        if (n <= 0) {
            return;
        }

        for (ssize_t off = 0; off < n;) {
            auto* e = (const inotify_event*)(buf + off);

            off += (ssize_t)sizeof(inotify_event) + e->len;

            if (!e->len) {
                continue;
            }

            StringView name(e->name);

            if (!name.startsWith("event"_sv)) {
                continue;
            }

            int idx = (int)StringView(name.begin() + 5, name.end()).stou();

            if (e->mask & IN_DELETE) {
                pathDrop(idx);
            } else if (pathAdd(idx)) {
                sysO << "imway: input device event"_sv << idx << " plugged"_sv << endL;
            }
        }
    }
}

LibinputSource::~LibinputSource() noexcept {
    if (inoFd >= 0) {
        ev_io_stop(loop, &inoIo);
        close(inoFd);
    }

    for (libinput_device* d : pathDevs) {
        if (d) {
            libinput_device_unref(d);
        }
    }

    if (li) {
        ev_io_stop(loop, &io);
        libinput_unref(li);
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
            case LIBINPUT_EVENT_DEVICE_ADDED: {
                libinput_device* dev = libinput_event_get_device(ev);

                configureDevice(dev);

                int idx = sysnameIndex(dev);

                if (idx >= 0 && !pathDevs[idx]) {
                    pathDevs[idx] = libinput_device_ref(dev);
                }

                break;
            }
            case LIBINPUT_EVENT_DEVICE_REMOVED: {
                // free the slot so a re-plugged device can come back; the
                // identity check matters: after a pathDrop + fast re-add the
                // stale removal must not clear the fresh device's slot
                libinput_device* dev = libinput_event_get_device(ev);
                int idx = sysnameIndex(dev);

                if (idx >= 0 && pathDevs[idx] == dev) {
                    libinput_device_unref(dev);
                    pathDevs[idx] = nullptr;
                    pathBits &= ~(1ull << idx);
                }

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
                bool wheel = libinput_event_get_type(ev) == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL;

                // sink units are wheel notches; v120 is only valid for wheels,
                // finger/continuous values come in scroll units, ~15 per notch
                double dx = 0, dy = 0;

                if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
                    dy = wheel ? libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL) / 120.0
                               : libinput_event_pointer_get_scroll_value(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL) / 15.0;
                }

                if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
                    dx = wheel ? libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL) / 120.0
                               : libinput_event_pointer_get_scroll_value(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL) / 15.0;
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
