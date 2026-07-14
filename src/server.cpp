#include "server.h"
#include "util.h"

#include "linux_dmabuf.h"
#include "kms.h"
#include "renderer.h"
#include "seat.h"

#include <string.h>
#include <time.h>

#include <wayland-server-protocol.h>

#include <imgui.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

using namespace stl;

u32 nowMsec() {
    timespec ts{};

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (u32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// --- методы модели (server.hpp) ---

bool Surface::inputContains(double sx, double sy) const {
    if (!inputRegionSet) {
        return true;
    }

    for (const RectI& r : inputRegion) {
        if (sx >= r.x && sy >= r.y && sx < r.x + r.w && sy < r.y + r.h) {
            return true;
        }
    }

    return false;
}

int Surface::viewW() const {
    return vp.hasDst ? vp.dw : vp.hasSrc ? (int)vp.sw : width;
}

int Surface::viewH() const {
    return vp.hasDst ? vp.dh : vp.hasSrc ? (int)vp.sh : height;
}

Surface* Surface::rootSurface() {
    Surface* s = this;

    // вверх по цепочке субповерхностей до корня
    while (s->sub && s->sub->parent) {
        s = s->sub->parent;
    }

    return s;
}

Toplevel* Surface::rootToplevel() {
    Surface* s = rootSurface();

    if (s->sub) { // сирота: родитель умер
        return nullptr;
    }

    return s->xdg ? s->xdg->toplevel : nullptr;
}

bool Subsurface::effectiveSync() const {
    for (const Subsurface* s = this; s; s = s->parent ? s->parent->sub : nullptr) {
        if (s->sync) {
            return true;
        }
    }

    return false;
}

// --- event loop ---

namespace {
    void wlIoCb(struct ev_loop*, ev_io* w, int) {
        auto* s = (Server*)w->data;

        wl_event_loop_dispatch(s->wlLoop, 0);
    }

    // Инвариант libwayland: не засыпать с несброшенными буферами клиентов.
    void flushCb(struct ev_loop*, ev_prepare* w, int) {
        auto* s = (Server*)w->data;

        wl_display_flush_clients(s->display);
    }

    void frameCb(struct ev_loop*, ev_timer* w, int) {
        ((Server*)w->data)->onFrameTick();
    }

    void signalCb(struct ev_loop* loop, ev_signal*, int) {
        ev_break(loop, EVBREAK_ALL);
    }

    void fireFrameCallbacks(Surface& s, u32 t) {
        // деструктор ресурса удаляет callback из frameCbs — забираем список до итерации
        Vector<wl_resource*> cbs;

        cbs.xchg(s.frameCbs);

        for (wl_resource* cb : cbs) {
            wl_callback_send_done(cb, t);
            wl_resource_destroy(cb);
        }

        for (Subsurface* c : s.stackBelow) {
            if (c->surface) {
                fireFrameCallbacks(*c->surface, t);
            }
        }

        for (Subsurface* c : s.stackAbove) {
            if (c->surface) {
                fireFrameCallbacks(*c->surface, t);
            }
        }
    }
}

void Server::init() {
    display = wl_display_create();
    STD_VERIFY(display);

    wlLoop = wl_display_get_event_loop(display);
    loop = ev_default_loop(0);

    surfaceAlloc = pool->make<ObjList<Surface>>(pool);
    subsurfaceAlloc = pool->make<ObjList<Subsurface>>(pool);
    xdgSurfaceAlloc = pool->make<ObjList<XdgSurface>>(pool);
    toplevelAlloc = pool->make<ObjList<Toplevel>>(pool);
    popupAlloc = pool->make<ObjList<Popup>>(pool);

    if (wl_display_add_socket(display, socketName) != 0) {
        Errno().raise(StringBuilder() << "wl socket "_sv << socketName
                                      << " failed (XDG_RUNTIME_DIR?)"_sv);
    }

    wl_display_init_shm(display);

    // kms до рендерера: размер output диктует режим дисплея
    if (!strcmp(backend, "kms")) {
        kms = Kms::create(pool, *this, drmDevice);
    }

    renderer = Renderer::create(pool, outW, outH);
    seat = Seat::create(pool, *this);

    if (kms) {
        STD_VERIFY(kms->start());

        try {
            input = InputLinux::create(pool, *this);
        } catch (...) {
            sysE << "imway: no input, mouse is dead: "_sv << Exception::current() << endL;
        }

        ImGui::GetIO().MouseDrawCursor = true; // композитный курсор
    }

    compositorCreateGlobals(*this);
    xdgShellCreateGlobal(*this);
    outputCreateGlobal(*this);
    seatCreateGlobal(*this);
    dataDeviceCreateGlobal(*this);
    xdgDecorationCreateGlobal(*this);
    viewporterCreateGlobal(*this);
    linuxDmabufCreateGlobal(*this);

    if (controlPath) {
        control = Control::create(pool, *this, controlPath);
    }

    ev_io_init(&wlIo, wlIoCb, wl_event_loop_get_fd(wlLoop), EV_READ);
    wlIo.data = this;
    ev_io_start(loop, &wlIo);

    ev_prepare_init(&flushPrepare, flushCb);
    flushPrepare.data = this;
    ev_prepare_start(loop, &flushPrepare);

    ev_timer_init(&frameTimer, frameCb, 0., 1.0 / hz);
    frameTimer.data = this;
    ev_timer_start(loop, &frameTimer);

    ev_signal_init(&sigInt, signalCb, SIGINT);
    ev_signal_start(loop, &sigInt);
    ev_signal_init(&sigTerm, signalCb, SIGTERM);
    ev_signal_start(loop, &sigTerm);

    sysO << "imway: socket "_sv << socketName << ", output "_sv << outW << "x"_sv << outH << "@"_sv
         << (i64)hz << endL;
}

void Server::onFrameTick() {
    // lavapipe — это CPU: без изменений кадр не рисуем вовсе
    if (needsFrame) {
        settleFrames = 3; // ImGui дорисует hover/анимации
    }

    bool active = needsFrame || settleFrames > 0;

    needsFrame = false;

    if (active) {
        settleFrames--;

        // загрузить свежие пиксели в текстуры (субповерхности тоже — у каждой своя)
        for (Surface* s : surfaces) {
            if (s->dirty && s->hasContent) {
                if (s->dmabufBuffer) {
                    renderer->importDmabuf(*s);
                } else {
                    renderer->uploadSurface(*s);
                }

                s->dirty = false;
            }
        }

        renderer->renderFrame(*this);

        if (kms) {
            kms->present(renderer->readbackData());
        }

        // frame callbacks — всем деревьям, показанным в кадре (попапам тоже,
        // GTK не рисует контент меню, пока не получит frame done)
        u32 t = nowMsec();

        for (Toplevel* tl : toplevels) {
            Surface* surf = tl->xdg ? tl->xdg->surface : nullptr;

            if (tl->mapped && surf) {
                fireFrameCallbacks(*surf, t);
            }
        }

        for (Popup* p : popups) {
            Surface* surf = p->xdg ? p->xdg->surface : nullptr;

            if (surf) {
                fireFrameCallbacks(*surf, t);
            }
        }

        // ресайз ImGui-окном: контент-регион разошёлся с размером поверхности
        for (Toplevel* tl : toplevels) {
            Surface* surf = tl->xdg ? tl->xdg->surface : nullptr;

            if (!tl->mapped || !surf || tl->desiredW <= 0) {
                continue;
            }

            bool differsView = tl->desiredW != surf->viewW() || tl->desiredH != surf->viewH();
            bool differsSent = tl->desiredW != tl->cfgW || tl->desiredH != tl->cfgH;

            if (differsView && differsSent) {
                xdgToplevelConfigureSize(*tl, tl->desiredW, tl->desiredH);
            }
        }
    }

    framesDone++;

    if (framesLimit > 0 && framesDone >= framesLimit) {
        ev_break(loop, EVBREAK_ALL);
    }
}

void Server::run() {
    ev_run(loop, 0);
}

void Server::finish() {
    // скриншот последнего кадра — до разрушения клиентов и рендерера
    if (screenshotPath && renderer) {
        renderer->screenshot(screenshotPath);
        sysO << "imway: screenshot: "_sv << screenshotPath << endL;
    }

    // клиенты умирают первыми: их деструкторы освобождают текстуры через renderer;
    // сами подсистемы умрут вместе с пулом (LIFO: control → input → seat → renderer → kms)
    if (display) {
        wl_display_destroy_clients(display);
        wl_display_destroy(display);
        display = nullptr;
    }
}
