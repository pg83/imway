#include "input.h"

#include "log.h"
#include "util.h"
#include "scene.h"
#include "pooled.h"
#include "session.h"
#include "composer.h"
#include "listener.h"
#include "input_sink.h"
#include "log_extern.h"

#include <std/ios/sys.h>
#include <std/dbg/verify.h>
#include <std/mem/obj_pool.h>

#include <ev.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <libinput.h>
#include <sys/inotify.h>

using namespace stl;

namespace {
    int openRestricted(const char* path, int, void* data);
    void closeRestricted(int fd, void* data);

    const libinput_interface liIface = {
        .open_restricted = openRestricted,
        .close_restricted = closeRestricted,
    };

    void inputIoCb(struct ev_loop*, ev_io* w, int);

    struct LibinputSource;

    struct CallInputSessionEnabled: Listener {
        LibinputSource* parent;

        CallInputSessionEnabled(LibinputSource* p);
        void onListen(void*) override;
    };

    struct CallInputSessionDisabled: Listener {
        LibinputSource* parent;

        CallInputSessionDisabled(LibinputSource* p);
        void onListen(void*) override;
    };

    struct CallInputSettings: Listener {
        LibinputSource* parent;

        CallInputSettings(LibinputSource* p);
        void onListen(void*) override;
    };

    struct LibinputSource: public InputSource {
        Composer* comp = nullptr;
        struct ev_loop* loop = nullptr;
        Session* session = nullptr;
        libinput* li = nullptr;

        // hotplug: inotify on /dev/input, one bit + device slot per eventN
        int inoFd = -1;
        u64 pathBits = 0;
        libinput_device* pathDevs[64] = {};

        LibinputSource(Composer& c);
        ~LibinputSource() noexcept;

        void setPointerSpeed(double s) override;
        double pointerSpeed() const override;

        bool pathAdd(int n);

        // the node vanished: yank the device now — libinput only notices a
        // dead fd on its next read, and a replug reusing the same number
        // must not find the slot still taken
        void pathDrop(int n);

        static int sysnameIndex(libinput_device* dev);

        void inotifyEvents();

        void sessionEnabled();
        void sessionDisabled();

        void applySettings();
        void registerDevice(libinput_device* dev);
        void dispatch();
    };

    CallInputSessionEnabled::CallInputSessionEnabled(LibinputSource* p)
        : parent(p)
    {
    }

    void CallInputSessionEnabled::onListen(void*) {
        parent->sessionEnabled();
    }

    CallInputSessionDisabled::CallInputSessionDisabled(LibinputSource* p)
        : parent(p)
    {
    }

    void CallInputSessionDisabled::onListen(void*) {
        parent->sessionDisabled();
    }

    CallInputSettings::CallInputSettings(LibinputSource* p)
        : parent(p)
    {
    }

    void CallInputSettings::onListen(void*) {
        parent->applySettings();
    }

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

    const InputDeviceSettings* deviceSettings(const Settings& settings, libinput_device* dev) {
        StringView id(libinput_device_get_sysname(dev));
        size_t count = settings.inputDeviceCount();

        for (size_t i = 0; i < count && i < Settings::inputDeviceCapacity; i++) {
            const InputDeviceSettings& value = settings.inputDevice(i);

            if (StringView(value.id) == id) {
                return &value;
            }
        }

        return nullptr;
    }

    bool inputToggle(InputToggle value, bool defaultValue) {
        return value == InputToggle::enabled || (value == InputToggle::automatic && defaultValue);
    }

    void configureDevice(libinput_device* dev, const Settings& settings) {
        const InputDeviceSettings* local = deviceSettings(settings, dev);
        double speed = local && local->enabled && local->pointerSpeedSet ? local->pointerSpeed : settings.pointerSpeed();
        bool natural = local && local->enabled && local->naturalScrollSet ? local->naturalScroll : inputToggle(settings.naturalScroll(), libinput_device_config_scroll_get_default_natural_scroll_enabled(dev));
        bool left = local && local->enabled && local->leftHandedSet ? local->leftHanded : inputToggle(settings.leftHanded(), libinput_device_config_left_handed_get_default(dev));

        if (libinput_device_config_tap_get_finger_count(dev) > 0) {
            libinput_device_config_tap_set_enabled(dev, settings.tapToClick() ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
        }

        if (libinput_device_config_accel_is_available(dev)) {
            libinput_device_config_accel_set_speed(dev, speed);

            u32 profiles = (u32)libinput_device_config_accel_get_profiles(dev);
            libinput_config_accel_profile profile = settings.pointerAccelProfile() == PointerAccelProfile::flat ? LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT : settings.pointerAccelProfile() == PointerAccelProfile::adaptive ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE : libinput_device_config_accel_get_default_profile(dev);

            if (profiles & (u32)profile) {
                libinput_device_config_accel_set_profile(dev, profile);
            }
        }

        if (libinput_device_config_scroll_has_natural_scroll(dev)) {
            libinput_device_config_scroll_set_natural_scroll_enabled(dev, natural);
        }

        if (libinput_device_config_left_handed_is_available(dev)) {
            libinput_device_config_left_handed_set(dev, left);
        }

        if (libinput_device_config_dwt_is_available(dev)) {
            bool enabled = inputToggle(settings.disableWhileTyping(), libinput_device_config_dwt_get_default_enabled(dev));

            libinput_device_config_dwt_set_enabled(dev, enabled ? LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);
        }

        if (libinput_device_config_middle_emulation_is_available(dev)) {
            bool enabled = inputToggle(settings.middleEmulation(), libinput_device_config_middle_emulation_get_default_enabled(dev));

            libinput_device_config_middle_emulation_set_enabled(dev, enabled ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
        }

        u32 clicks = (u32)libinput_device_config_click_get_methods(dev);
        libinput_config_click_method click = settings.touchpadClickMethod() == TouchpadClickMethod::buttonAreas ? LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS : settings.touchpadClickMethod() == TouchpadClickMethod::clickfinger ? LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER : libinput_device_config_click_get_default_method(dev);

        if (clicks & (u32)click) {
            libinput_device_config_click_set_method(dev, click);
        }

        u32 scrolls = (u32)libinput_device_config_scroll_get_methods(dev);
        libinput_config_scroll_method scroll = settings.touchpadScrollMethod() == TouchpadScrollMethod::twoFinger ? LIBINPUT_CONFIG_SCROLL_2FG : settings.touchpadScrollMethod() == TouchpadScrollMethod::edge ? LIBINPUT_CONFIG_SCROLL_EDGE : settings.touchpadScrollMethod() == TouchpadScrollMethod::button ? LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN : libinput_device_config_scroll_get_default_method(dev);

        if (scrolls & (u32)scroll) {
            libinput_device_config_scroll_set_method(dev, scroll);
        }
    }
}

// path backend only: the udev one needs a running udevd for enumeration AND
// hotplug; a direct /dev/input scan plus inotify behaves the same either way
namespace {
    void libinputLog(struct libinput* li, enum libinput_log_priority, const char* fmt, va_list args) {
        auto* source = (LibinputSource*)libinput_get_user_data(li);

        externVLog(*source->comp->log, "libinput"_sv, fmt, args);
    }
}

LibinputSource::LibinputSource(Composer& c)
    : comp(&c)
    , loop(c.loop)
    , session(c.session)
{
    li = libinput_path_create_context(&liIface, this);
    STD_VERIFY(li);
    libinput_log_set_handler(li, libinputLog);

    int devices = 0;

    for (int i = 0; i < 64; i++) {
        if (pathAdd(i)) {
            devices++;
        }
    }

    inoFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

    if (inoFd >= 0) {
        int heldFd = inoFd;

        pooledGuard(*c.pool, [heldFd] {
            close(heldFd);
        });
    }

    if (inoFd >= 0 && inotify_add_watch(inoFd, "/dev/input", IN_CREATE | IN_ATTRIB | IN_DELETE) >= 0) {
        ev_io* inotifyIo = c.pool->make<ev_io>();
        struct ev_loop* heldLoop = loop;

        pooledGuard(*c.pool, [heldLoop, inotifyIo] {
            if (ev_is_active(inotifyIo)) {
                ev_io_stop(heldLoop, inotifyIo);
            }
        });
        ev_io_init(inotifyIo, inotifyCb, inoFd, EV_READ);
        inotifyIo->data = this;
        ev_io_start(loop, inotifyIo);
    }

    c.sessionEnabledListeners.pushBack(c.pool->make<CallInputSessionEnabled>(this));
    c.sessionDisabledListeners.pushBack(c.pool->make<CallInputSessionDisabled>(this));
    c.settings->addPointerSpeedListener(c.pool->make<CallInputSettings>(this));
    c.settings->addPointerAccelProfileListener(c.pool->make<CallInputSettings>(this));
    c.settings->addTapToClickListener(c.pool->make<CallInputSettings>(this));
    c.settings->addNaturalScrollListener(c.pool->make<CallInputSettings>(this));
    c.settings->addLeftHandedListener(c.pool->make<CallInputSettings>(this));
    c.settings->addDisableWhileTypingListener(c.pool->make<CallInputSettings>(this));
    c.settings->addMiddleEmulationListener(c.pool->make<CallInputSettings>(this));
    c.settings->addTouchpadClickMethodListener(c.pool->make<CallInputSettings>(this));
    c.settings->addTouchpadScrollMethodListener(c.pool->make<CallInputSettings>(this));
    c.settings->addInputDeviceCountListener(c.pool->make<CallInputSettings>(this));

    for (size_t i = 0; i < Settings::inputDeviceCapacity; i++) {
        c.settings->addInputDeviceListener(i, c.pool->make<CallInputSettings>(this));
    }

    ev_io* inputIo = c.pool->make<ev_io>();
    struct ev_loop* heldLoop = loop;

    pooledGuard(*c.pool, [heldLoop, inputIo] {
        if (ev_is_active(inputIo)) {
            ev_io_stop(heldLoop, inputIo);
        }
    });
    ev_io_init(inputIo, inputIoCb, libinput_get_fd(li), EV_READ);
    inputIo->data = this;
    ev_io_start(loop, inputIo);
    dispatch();
    *(comp->log) << "imway: libinput ready, "_sv << devices << " devices"_sv << endL;
}

bool LibinputSource::pathAdd(int n) {
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

void LibinputSource::pathDrop(int n) {
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

void LibinputSource::setPointerSpeed(double s) {
    comp->settings->setPointerSpeed((float)(s < -1. ? -1. : s > 1. ? 1. : s));
}

double LibinputSource::pointerSpeed() const {
    return comp->settings->pointerSpeed();
}

int LibinputSource::sysnameIndex(libinput_device* dev) {
    StringView sys(libinput_device_get_sysname(dev));

    if (!sys.startsWith("event"_sv)) {
        return -1;
    }

    int idx = (int)StringView(sys.begin() + 5, sys.end()).stou();

    return idx >= 0 && idx < 64 ? idx : -1;
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
                *(comp->log) << "imway: input device event"_sv << idx << " plugged"_sv << endL;
            }
        }
    }
}

void LibinputSource::sessionEnabled() {
    libinput_resume(li);
}

void LibinputSource::sessionDisabled() {
    libinput_suspend(li);
}

void LibinputSource::applySettings() {
    for (libinput_device* dev : pathDevs) {
        if (dev) {
            configureDevice(dev, *comp->settings);
        }
    }
}

void LibinputSource::registerDevice(libinput_device* dev) {
    StringView id(libinput_device_get_sysname(dev));
    size_t count = comp->settings->inputDeviceCount();

    for (size_t i = 0; i < count && i < Settings::inputDeviceCapacity; i++) {
        if (StringView(comp->settings->inputDevice(i).id) == id) {
            return;
        }
    }

    if (count >= Settings::inputDeviceCapacity) {
        return;
    }

    InputDeviceSettings value;
    StringView name(libinput_device_get_name(dev));
    size_t idLen = id.length() < sizeof(value.id) - 1 ? id.length() : sizeof(value.id) - 1;
    size_t nameLen = name.length() < sizeof(value.name) - 1 ? name.length() : sizeof(value.name) - 1;

    memcpy(value.id, id.begin(), idLen);
    value.id[idLen] = 0;
    memcpy(value.name, name.begin(), nameLen);
    value.name[nameLen] = 0;
    comp->settings->setInputDevice(count, value);
    comp->settings->setInputDeviceCount(count + 1);
}

// churn state: devices come and go with hotplug, the context holds session
// callbacks into this impl — both stay destructor work. The pooled watchers
// stop after this; stopping a watcher over an already-dead fd is harmless.
LibinputSource::~LibinputSource() noexcept {
    for (libinput_device* d : pathDevs) {
        if (d) {
            libinput_device_unref(d);
        }
    }

    if (li) {
        libinput_unref(li);
    }
}

namespace {
    u32 tabletToolWireType(libinput_tablet_tool* tool) {
        switch (libinput_tablet_tool_get_type(tool)) {
            case LIBINPUT_TABLET_TOOL_TYPE_ERASER:
                return 0x141;
            case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:
                return 0x142;
            case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:
                return 0x143;
            case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:
                return 0x144;
            case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:
                return 0x146;
            case LIBINPUT_TABLET_TOOL_TYPE_LENS:
                return 0x147;
            default:
                return 0x140; // pen
        }
    }

    void tabletToolAxes(libinput_event_tablet_tool* t, TabletToolEvent& tev, int outW, int outH) {
        tev.toolType = tabletToolWireType(libinput_event_tablet_tool_get_tool(t));
        tev.x = libinput_event_tablet_tool_get_x_transformed(t, (u32)outW);
        tev.y = libinput_event_tablet_tool_get_y_transformed(t, (u32)outH);

        if (libinput_event_tablet_tool_pressure_has_changed(t)) {
            tev.pressureSet = true;
            tev.pressure = libinput_event_tablet_tool_get_pressure(t);
        }

        if (libinput_event_tablet_tool_distance_has_changed(t)) {
            tev.distanceSet = true;
            tev.distance = libinput_event_tablet_tool_get_distance(t);
        }

        if (libinput_event_tablet_tool_tilt_x_has_changed(t) || libinput_event_tablet_tool_tilt_y_has_changed(t)) {
            tev.tiltSet = true;
            tev.tiltX = libinput_event_tablet_tool_get_tilt_x(t);
            tev.tiltY = libinput_event_tablet_tool_get_tilt_y(t);
        }

        if (libinput_event_tablet_tool_rotation_has_changed(t)) {
            tev.rotationSet = true;
            tev.rotation = libinput_event_tablet_tool_get_rotation(t);
        }

        if (libinput_event_tablet_tool_slider_has_changed(t)) {
            tev.sliderSet = true;
            tev.slider = libinput_event_tablet_tool_get_slider_position(t);
        }

        if (libinput_event_tablet_tool_wheel_has_changed(t)) {
            tev.wheelSet = true;
            tev.wheelDegrees = libinput_event_tablet_tool_get_wheel_delta(t);
            tev.wheelClicks = (i32)libinput_event_tablet_tool_get_wheel_delta_discrete(t);
        }
    }
}

void LibinputSource::dispatch() {
    libinput_dispatch(li);

    libinput_event* ev;

    while ((ev = libinput_get_event(li))) {
        switch (libinput_event_get_type(ev)) {
            case LIBINPUT_EVENT_POINTER_MOTION: {
                auto* p = libinput_event_get_pointer_event(ev);
                PointerMotionEvent motion;

                motion.kind = PointerMotionKind::relative;
                motion.dx = libinput_event_pointer_get_dx(p);
                motion.dy = libinput_event_pointer_get_dy(p);
                motion.dxRaw = libinput_event_pointer_get_dx_unaccelerated(p);
                motion.dyRaw = libinput_event_pointer_get_dy_unaccelerated(p);
                comp->entry->pointerMotion(motion);

                break;
            }
            case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
                auto* p = libinput_event_get_pointer_event(ev);
                PointerMotionEvent motion;

                // normalized: the cursor owner maps this to the screen
                motion.kind = PointerMotionKind::absolute;
                motion.x = libinput_event_pointer_get_absolute_x_transformed(p, 1);
                motion.y = libinput_event_pointer_get_absolute_y_transformed(p, 1);
                comp->entry->pointerMotion(motion);

                break;
            }
            case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
                comp->entry->swipeBegin((u32)libinput_event_gesture_get_finger_count(libinput_event_get_gesture_event(ev)));
                break;
            case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE: {
                auto* g = libinput_event_get_gesture_event(ev);

                comp->entry->swipeUpdate(libinput_event_gesture_get_dx(g), libinput_event_gesture_get_dy(g));

                break;
            }
            case LIBINPUT_EVENT_GESTURE_SWIPE_END:
                comp->entry->swipeEnd(libinput_event_gesture_get_cancelled(libinput_event_get_gesture_event(ev)) != 0);
                break;
            case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
                comp->entry->pinchBegin((u32)libinput_event_gesture_get_finger_count(libinput_event_get_gesture_event(ev)));
                break;
            case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE: {
                auto* g = libinput_event_get_gesture_event(ev);

                comp->entry->pinchUpdate(libinput_event_gesture_get_dx(g), libinput_event_gesture_get_dy(g), libinput_event_gesture_get_scale(g), libinput_event_gesture_get_angle_delta(g));

                break;
            }
            case LIBINPUT_EVENT_GESTURE_PINCH_END:
                comp->entry->pinchEnd(libinput_event_gesture_get_cancelled(libinput_event_get_gesture_event(ev)) != 0);
                break;
            case LIBINPUT_EVENT_GESTURE_HOLD_BEGIN:
                comp->entry->holdBegin((u32)libinput_event_gesture_get_finger_count(libinput_event_get_gesture_event(ev)));
                break;
            case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY: {
                auto* t = libinput_event_get_tablet_tool_event(ev);
                TabletToolEvent tev;

                tev.phase = libinput_event_tablet_tool_get_proximity_state(t) == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN ? TabletPhase::proximityIn : TabletPhase::proximityOut;
                tabletToolAxes(t, tev, comp->scene->outW, comp->scene->outH);
                comp->entry->tabletTool(tev);

                break;
            }
            case LIBINPUT_EVENT_TABLET_TOOL_TIP: {
                auto* t = libinput_event_get_tablet_tool_event(ev);
                TabletToolEvent tev;

                tev.phase = libinput_event_tablet_tool_get_tip_state(t) == LIBINPUT_TABLET_TOOL_TIP_DOWN ? TabletPhase::tipDown : TabletPhase::tipUp;
                tabletToolAxes(t, tev, comp->scene->outW, comp->scene->outH);
                comp->entry->tabletTool(tev);

                break;
            }
            case LIBINPUT_EVENT_TABLET_TOOL_AXIS: {
                auto* t = libinput_event_get_tablet_tool_event(ev);
                TabletToolEvent tev;

                tabletToolAxes(t, tev, comp->scene->outW, comp->scene->outH);
                comp->entry->tabletTool(tev);

                break;
            }
            case LIBINPUT_EVENT_TABLET_TOOL_BUTTON: {
                auto* t = libinput_event_get_tablet_tool_event(ev);
                TabletToolEvent tev;

                tabletToolAxes(t, tev, comp->scene->outW, comp->scene->outH);
                tev.buttonSet = true;
                tev.button = libinput_event_tablet_tool_get_button(t);
                tev.buttonPressed = libinput_event_tablet_tool_get_button_state(t) == LIBINPUT_BUTTON_STATE_PRESSED;
                comp->entry->tabletTool(tev);

                break;
            }
            case LIBINPUT_EVENT_GESTURE_HOLD_END:
                comp->entry->holdEnd(libinput_event_gesture_get_cancelled(libinput_event_get_gesture_event(ev)) != 0);
                break;
            case LIBINPUT_EVENT_DEVICE_ADDED: {
                libinput_device* dev = libinput_event_get_device(ev);
                int idx = sysnameIndex(dev);

                if (idx >= 0 && !pathDevs[idx]) {
                    pathDevs[idx] = libinput_device_ref(dev);
                }

                registerDevice(dev);
                configureDevice(dev, *comp->settings);

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

                comp->entry->button(libinput_event_pointer_get_button(p), libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED);

                break;
            }
            case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
            case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
            case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS: {
                auto* p = libinput_event_get_pointer_event(ev);
                bool wheel = libinput_event_get_type(ev) == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL;
                bool finger = libinput_event_get_type(ev) == LIBINPUT_EVENT_POINTER_SCROLL_FINGER;

                // sink units are wheel notches; v120 is only valid for wheels,
                // finger/continuous values come in scroll units, ~15 per notch
                ScrollEvent scroll;

                scroll.source = wheel ? ScrollSource::wheel : finger ? ScrollSource::finger : ScrollSource::continuous;

                if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
                    double raw = wheel ? libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL) : libinput_event_pointer_get_scroll_value(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

                    scroll.dy = raw / (wheel ? 120.0 : 15.0);
                    scroll.discreteY = wheel ? (i32)(raw / 120.0) : 0;
                    scroll.value120Y = wheel ? (i32)raw : 0;
                    scroll.stopY = !wheel && raw == 0;
                }

                if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
                    double raw = wheel ? libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL) : libinput_event_pointer_get_scroll_value(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);

                    scroll.dx = raw / (wheel ? 120.0 : 15.0);
                    scroll.discreteX = wheel ? (i32)(raw / 120.0) : 0;
                    scroll.value120X = wheel ? (i32)raw : 0;
                    scroll.stopX = !wheel && raw == 0;
                }

                if (scroll.dx != 0 || scroll.dy != 0 || scroll.stopX || scroll.stopY) {
                    comp->entry->scroll(scroll);
                }

                break;
            }
            case LIBINPUT_EVENT_KEYBOARD_KEY: {
                auto* k = libinput_event_get_keyboard_event(ev);

                comp->entry->key(libinput_event_keyboard_get_key(k), libinput_event_keyboard_get_key_state(k) == LIBINPUT_KEY_STATE_PRESSED);

                break;
            }
            default:
                break;
        }

        libinput_event_destroy(ev);
    }
}

InputSource* InputSource::createLibinput(Composer& c) {
    return c.pool->make<LibinputSource>(c);
}
