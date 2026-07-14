#include "seat.h"
#include "server.h"
#include "util.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <imgui.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    constexpr u32 kSeatVersion = 5;

    void resDestroy(wl_client*, wl_resource* res) {
        wl_resource_destroy(res);
    }

    struct SeatImpl: public Seat {
        Server* server = nullptr;

        Vector<wl_resource*> keyboards; // все wl_keyboard всех клиентов
        Vector<wl_resource*> pointers;

        xkb_context* xkb = nullptr;
        xkb_keymap* keymap = nullptr;
        xkb_state* xkbState = nullptr;
        int keymapFd = -1;
        u32 keymapSize = 0;

        Toplevel* kbFocus = nullptr;
        Surface* kbOverride = nullptr; // grab-попап: клавиатура идёт сюда, не в kbFocus
        Surface* ptrFocus = nullptr;   // поверхность (в т.ч. суб-), куда идут pointer-события
        int buttonsDown = 0;           // implicit grab, пока >0 — ptrFocus залочен

        double curX = 0, curY = 0; // координаты output
        Vector<u32> pressedKeys;
        u32 modsDepressed = 0, modsLatched = 0, modsLocked = 0, modsGroup = 0;

        SeatImpl(Server& srv)
            : server(&srv)
        {
            xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            STD_VERIFY(xkb);

            keymap = xkb_keymap_new_from_names(xkb, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
            STD_VERIFY(keymap);

            xkbState = xkb_state_new(keymap);

            char* str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);

            keymapSize = (u32)strlen(str) + 1;
            keymapFd = memfd_create("imway-keymap", 0);

            bool written = keymapFd >= 0 && write(keymapFd, str, keymapSize) == (ssize_t)keymapSize;

            free(str);
            STD_VERIFY(written); // keymap fd не создался
        }

        ~SeatImpl() noexcept override {
            if (keymapFd >= 0) {
                close(keymapFd);
            }

            if (xkbState) {
                xkb_state_unref(xkbState);
            }

            if (keymap) {
                xkb_keymap_unref(keymap);
            }

            if (xkb) {
                xkb_context_unref(xkb);
            }
        }

        bool sameClient(wl_resource* res, Toplevel* t) {
            return t && t->xdg && t->xdg->surface &&
                   wl_resource_get_client(res) == wl_resource_get_client(t->xdg->surface->res);
        }

        bool sameClientS(wl_resource* res, Surface* s) {
            return s && wl_resource_get_client(res) == wl_resource_get_client(s->res);
        }

        // топовая hovered-поверхность дерева: последняя в порядке отрисовки
        // (stackBelow → сама → stackAbove) перекрывает предыдущие;
        // поверхности с input region мимо точки — прозрачны для ввода
        Surface* pickInTree(Surface& s) {
            Surface* found = nullptr;

            for (Subsurface* c : s.stackBelow) {
                if (c->surface && c->surface->hasContent) {
                    if (Surface* f = pickInTree(*c->surface)) {
                        found = f;
                    }
                }
            }

            if (s.hovered && s.inputContains(curX - s.imgX, curY - s.imgY)) {
                found = &s;
            }

            for (Subsurface* c : s.stackAbove) {
                if (c->surface && c->surface->hasContent) {
                    if (Surface* f = pickInTree(*c->surface)) {
                        found = f;
                    }
                }
            }

            return found;
        }

        Surface* pickPointerTarget() {
            // hovered-флаги выставлены ImGui в последнем кадре (между окнами z-order
            // учтён, внутри окна поздние Image перекрывают ранние — берём последний
            // hovered в дереве). Попапы сверху: последний созданный — самый верхний.
            for (size_t i = server->popups.length(); i > 0; i--) {
                Popup* p = server->popups[i - 1];

                if (!p->mapped || !p->xdg || !p->xdg->surface) {
                    continue;
                }

                if (Surface* s = pickInTree(*p->xdg->surface)) {
                    return s;
                }
            }

            for (Toplevel* t : server->toplevels) {
                if (!t->mapped || !t->xdg || !t->xdg->surface) {
                    continue;
                }

                if (Surface* s = pickInTree(*t->xdg->surface)) {
                    return s;
                }
            }

            return nullptr;
        }

        void pointerSetFocus(Surface* s, double sx, double sy) {
            if (ptrFocus == s) {
                return;
            }

            if (ptrFocus) {
                u32 serial = wl_display_next_serial(server->display);

                for (wl_resource* p : pointers) {
                    if (sameClientS(p, ptrFocus)) {
                        wl_pointer_send_leave(p, serial, ptrFocus->res);

                        if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                            wl_pointer_send_frame(p);
                        }
                    }
                }
            }

            ptrFocus = s;

            if (s) {
                u32 serial = wl_display_next_serial(server->display);

                for (wl_resource* p : pointers) {
                    if (sameClientS(p, s)) {
                        wl_pointer_send_enter(p, serial, s->res, wl_fixed_from_double(sx),
                                              wl_fixed_from_double(sy));

                        if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                            wl_pointer_send_frame(p);
                        }
                    }
                }
            }
        }

        void handleMotion(double x, double y) override {
            curX = x;
            curY = y;
            server->needsFrame = true;
            ImGui::GetIO().AddMousePosEvent((float)x, (float)y);

            Surface* target = buttonsDown > 0 ? ptrFocus : pickPointerTarget();

            if (target != ptrFocus) {
                double sx = target ? x - target->imgX : 0, sy = target ? y - target->imgY : 0;

                pointerSetFocus(target, sx, sy);

                return;
            }

            if (!ptrFocus) {
                return;
            }

            double sx = x - ptrFocus->imgX;
            double sy = y - ptrFocus->imgY;
            u32 t = nowMsec();

            for (wl_resource* p : pointers) {
                if (sameClientS(p, ptrFocus)) {
                    wl_pointer_send_motion(p, t, wl_fixed_from_double(sx),
                                           wl_fixed_from_double(sy));

                    if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                        wl_pointer_send_frame(p);
                    }
                }
            }
        }

        void handleButton(u32 button, bool pressed) override {
            int imguiBtn = button == BTN_LEFT ? 0 : button == BTN_RIGHT ? 1 : 2;

            server->needsFrame = true;
            ImGui::GetIO().AddMouseButtonEvent(imguiBtn, pressed);

            // hovered-флаги могли освежиться кадрами после последнего motion —
            // без этого press после одиночного motion уходит мимо клиента
            if (pressed && buttonsDown == 0) {
                Surface* target = pickPointerTarget();

                if (target != ptrFocus) {
                    pointerSetFocus(target, target ? curX - target->imgX : 0,
                                    target ? curY - target->imgY : 0);
                }
            }

            // grab-попапы: клик мимо — закрыть (каскадно, сверху вниз до попавшего)
            if (pressed) {
                for (size_t i = server->popups.length(); i > 0; i--) {
                    Popup* p = server->popups[i - 1];

                    if (!p->mapped || !p->grab) {
                        continue;
                    }

                    Surface* proot = p->xdg ? p->xdg->surface : nullptr;

                    if (ptrFocus && proot && ptrFocus->rootSurface() == proot) {
                        break;
                    }

                    xdgPopupDismiss(*p);
                }
            }

            // click-to-focus: клавиатурный фокус — toplevel'у корня дерева
            if (pressed && ptrFocus) {
                if (Toplevel* t = ptrFocus->rootToplevel()) {
                    focusToplevel(t);
                }
            }

            if (ptrFocus) {
                u32 serial = wl_display_next_serial(server->display);
                u32 t = nowMsec();

                for (wl_resource* p : pointers) {
                    if (sameClientS(p, ptrFocus)) {
                        wl_pointer_send_button(p, serial, t, button,
                                               pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                                       : WL_POINTER_BUTTON_STATE_RELEASED);

                        if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                            wl_pointer_send_frame(p);
                        }
                    }
                }
            }

            buttonsDown += pressed ? 1 : -1;

            if (buttonsDown < 0) {
                buttonsDown = 0;
            }
        }

        void handleScroll(double value) override {
            server->needsFrame = true;
            ImGui::GetIO().AddMouseWheelEvent(0.f, (float)-value);

            if (!ptrFocus) {
                return;
            }

            u32 t = nowMsec();

            for (wl_resource* p : pointers) {
                if (sameClientS(p, ptrFocus)) {
                    wl_pointer_send_axis(p, t, WL_POINTER_AXIS_VERTICAL_SCROLL,
                                         wl_fixed_from_double(value * 15.0));

                    if (wl_resource_get_version(p) >= WL_POINTER_FRAME_SINCE_VERSION) {
                        wl_pointer_send_frame(p);
                    }
                }
            }
        }

        // куда сейчас идёт клавиатура (override > kbFocus)
        wl_resource* kbTargetRes() {
            if (kbOverride) {
                return kbOverride->res;
            }

            if (kbFocus && kbFocus->xdg && kbFocus->xdg->surface) {
                return kbFocus->xdg->surface->res;
            }

            return nullptr;
        }

        void kbSendLeave(wl_resource* target) {
            if (!target) {
                return;
            }

            u32 serial = wl_display_next_serial(server->display);

            for (wl_resource* k : keyboards) {
                if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
                    wl_keyboard_send_leave(k, serial, target);
                }
            }
        }

        void kbSendEnter(wl_resource* target) {
            if (!target) {
                return;
            }

            u32 serial = wl_display_next_serial(server->display);
            wl_array keys;

            wl_array_init(&keys);

            for (u32 kc : pressedKeys) {
                *(u32*)wl_array_add(&keys, sizeof(u32)) = kc;
            }

            for (wl_resource* k : keyboards) {
                if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
                    wl_keyboard_send_enter(k, serial, target, &keys);
                    wl_keyboard_send_modifiers(k, wl_display_next_serial(server->display),
                                               modsDepressed, modsLatched, modsLocked, modsGroup);
                }
            }

            wl_array_release(&keys);
        }

        void updateModifiers() {
            u32 dep = xkb_state_serialize_mods(xkbState, XKB_STATE_MODS_DEPRESSED);
            u32 lat = xkb_state_serialize_mods(xkbState, XKB_STATE_MODS_LATCHED);
            u32 lock = xkb_state_serialize_mods(xkbState, XKB_STATE_MODS_LOCKED);
            u32 grp = xkb_state_serialize_layout(xkbState, XKB_STATE_LAYOUT_EFFECTIVE);

            if (dep == modsDepressed && lat == modsLatched && lock == modsLocked &&
                grp == modsGroup) {
                return;
            }

            modsDepressed = dep;
            modsLatched = lat;
            modsLocked = lock;
            modsGroup = grp;

            wl_resource* target = kbTargetRes();

            if (!target) {
                return;
            }

            u32 serial = wl_display_next_serial(server->display);

            for (wl_resource* k : keyboards) {
                if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
                    wl_keyboard_send_modifiers(k, serial, dep, lat, lock, grp);
                }
            }
        }

        void handleKey(u32 code, bool pressed) override {
            server->needsFrame = true;
            xkb_state_update_key(xkbState, code + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

            if (pressed) {
                pressedKeys.pushBack(code);
            } else {
                removeOne(pressedKeys, code);
            }

            if (wl_resource* target = kbTargetRes()) {
                u32 serial = wl_display_next_serial(server->display);
                u32 t = nowMsec();

                for (wl_resource* k : keyboards) {
                    if (wl_resource_get_client(k) == wl_resource_get_client(target)) {
                        wl_keyboard_send_key(k, serial, t, code,
                                             pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                                     : WL_KEYBOARD_KEY_STATE_RELEASED);
                    }
                }
            }

            updateModifiers();
        }

        void focusToplevel(Toplevel* t) override {
            if (kbFocus == t) {
                return;
            }

            if (kbFocus && kbFocus->xdg && kbFocus->xdg->surface) {
                u32 serial = wl_display_next_serial(server->display);

                for (wl_resource* k : keyboards) {
                    if (sameClient(k, kbFocus)) {
                        wl_keyboard_send_leave(k, serial, kbFocus->xdg->surface->res);
                    }
                }
            }

            kbFocus = t;

            if (t && t->xdg && t->xdg->surface) {
                kbSendEnter(t->xdg->surface->res);
                sysO << "imway: focus -> "_sv << (const char*)t->title << endL;
            }
        }

        void popupGrabStart(Popup* p) override {
            if (!p->xdg || !p->xdg->surface) {
                return;
            }

            kbSendLeave(kbTargetRes());
            kbOverride = p->xdg->surface;
            kbSendEnter(kbOverride->res);
        }

        void popupGone(Popup* p) override {
            Surface* s = p->xdg ? p->xdg->surface : nullptr;

            if (s && ptrFocus && ptrFocus->rootSurface() == s) {
                ptrFocus = nullptr;
                buttonsDown = 0;
            }

            if (s && kbOverride == s) {
                kbSendLeave(kbOverride->res);
                kbOverride = nullptr;
                kbSendEnter(kbTargetRes()); // клавиатура возвращается toplevel'у
            }
        }

        void surfaceGone(Surface* s) override {
            if (ptrFocus == s) {
                ptrFocus = nullptr;
                buttonsDown = 0;
            }
        }

        void toplevelGone(Toplevel* t) override {
            if (ptrFocus && ptrFocus->rootToplevel() == t) {
                ptrFocus = nullptr;
                buttonsDown = 0;
            }

            if (kbFocus == t) {
                kbFocus = nullptr;

                // отдать фокус последнему замапленному
                for (size_t i = server->toplevels.length(); i > 0; i--) {
                    Toplevel* other = server->toplevels[i - 1];

                    if (other != t && other->mapped) {
                        focusToplevel(other);

                        break;
                    }
                }
            }
        }
    };

    // --- wl_pointer / wl_keyboard / wl_touch ресурсы ---

    SeatImpl* seatOf(wl_resource* res) {
        return (SeatImpl*)wl_resource_get_user_data(res);
    }

    void pointerSetCursor(wl_client*, wl_resource*, u32, wl_resource*, i32, i32) {
        // курсоры клиентов игнорируем: курсор рисует ImGui
    }

    const struct wl_pointer_interface pointerImpl = {
        .set_cursor = pointerSetCursor,
        .release = resDestroy,
    };

    const struct wl_keyboard_interface keyboardImpl = {.release = resDestroy};
    const struct wl_touch_interface touchImpl = {.release = resDestroy};

    void pointerResourceDestroyed(wl_resource* res) {
        removeOne(seatOf(res)->pointers, res);
    }

    void keyboardResourceDestroyed(wl_resource* res) {
        removeOne(seatOf(res)->keyboards, res);
    }

    void seatGetPointer(wl_client* client, wl_resource* res, u32 id) {
        SeatImpl* seat = seatOf(res);
        wl_resource* p =
            wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(res), id);

        if (!p) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(p, &pointerImpl, seat, pointerResourceDestroyed);
        seat->pointers.pushBack(p);
    }

    void seatGetKeyboard(wl_client* client, wl_resource* res, u32 id) {
        SeatImpl* seat = seatOf(res);
        wl_resource* k =
            wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(res), id);

        if (!k) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(k, &keyboardImpl, seat, keyboardResourceDestroyed);
        seat->keyboards.pushBack(k);

        wl_keyboard_send_keymap(k, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, seat->keymapFd,
                                seat->keymapSize);

        if (wl_resource_get_version(k) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
            wl_keyboard_send_repeat_info(k, 25, 600);
        }

        // если фокус уже у этого клиента — новая клавиатура должна получить enter
        SeatImpl& s = *seat;

        if (s.kbFocus && s.kbFocus->xdg && s.kbFocus->xdg->surface &&
            wl_resource_get_client(s.kbFocus->xdg->surface->res) == client) {
            wl_array keys;

            wl_array_init(&keys);

            for (u32 kc : s.pressedKeys) {
                *(u32*)wl_array_add(&keys, sizeof(u32)) = kc;
            }

            wl_keyboard_send_enter(k, wl_display_next_serial(s.server->display),
                                   s.kbFocus->xdg->surface->res, &keys);
            wl_array_release(&keys);
            wl_keyboard_send_modifiers(k, wl_display_next_serial(s.server->display),
                                       s.modsDepressed, s.modsLatched, s.modsLocked, s.modsGroup);
        }
    }

    void seatGetTouch(wl_client* client, wl_resource* res, u32 id) {
        wl_resource* t =
            wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(res), id);

        if (t) {
            wl_resource_set_implementation(t, &touchImpl, nullptr, nullptr);
        }
    }

    const struct wl_seat_interface seatImpl = {
        .get_pointer = seatGetPointer,
        .get_keyboard = seatGetKeyboard,
        .get_touch = seatGetTouch,
        .release = resDestroy,
    };

    void seatBind(wl_client* client, void* data, u32 version, u32 id) {
        wl_resource* res = wl_resource_create(client, &wl_seat_interface, version, id);

        if (!res) {
            wl_client_post_no_memory(client);

            return;
        }

        wl_resource_set_implementation(res, &seatImpl, data, nullptr);
        wl_seat_send_capabilities(res, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

        if (version >= WL_SEAT_NAME_SINCE_VERSION) {
            wl_seat_send_name(res, "seat0");
        }
    }
}

Seat::~Seat() noexcept {
}

Seat* Seat::create(ObjPool* pool, Server& server) {
    return pool->make<SeatImpl>(server);
}

void seatCreateGlobal(Server& server) {
    wl_global_create(server.display, &wl_seat_interface, kSeatVersion, (SeatImpl*)server.seat,
                     seatBind);
}
