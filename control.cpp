#include "composer.h"
#include "log.h"
#include "control.h"
#include "icon.h"
#include "input_sink.h"
#include "pooled.h"
#include "pooled_ev.h"
#include "pooled_fd.h"
#include "renderer.h"
#include "intr_list.h"
#include "output.h"
#include "scene.h"
#include "util.h"

#include <ev.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <std/dbg/verify.h>
#include <std/ios/out_fd.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/sys/fd.h>

using namespace stl;

namespace {
    bool asciiToKey(char c, u32& code, bool& shift) {
        static const struct {
            char ch;
            u32 code;
            bool shift;
        } table[] = {
            {'a', KEY_A, 0},
            {'b', KEY_B, 0},
            {'c', KEY_C, 0},
            {'d', KEY_D, 0},
            {'e', KEY_E, 0},
            {'f', KEY_F, 0},
            {'g', KEY_G, 0},
            {'h', KEY_H, 0},
            {'i', KEY_I, 0},
            {'j', KEY_J, 0},
            {'k', KEY_K, 0},
            {'l', KEY_L, 0},
            {'m', KEY_M, 0},
            {'n', KEY_N, 0},
            {'o', KEY_O, 0},
            {'p', KEY_P, 0},
            {'q', KEY_Q, 0},
            {'r', KEY_R, 0},
            {'s', KEY_S, 0},
            {'t', KEY_T, 0},
            {'u', KEY_U, 0},
            {'v', KEY_V, 0},
            {'w', KEY_W, 0},
            {'x', KEY_X, 0},
            {'y', KEY_Y, 0},
            {'z', KEY_Z, 0},
            {'1', KEY_1, 0},
            {'2', KEY_2, 0},
            {'3', KEY_3, 0},
            {'4', KEY_4, 0},
            {'5', KEY_5, 0},
            {'6', KEY_6, 0},
            {'7', KEY_7, 0},
            {'8', KEY_8, 0},
            {'9', KEY_9, 0},
            {'0', KEY_0, 0},
            {' ', KEY_SPACE, 0},
            {'-', KEY_MINUS, 0},
            {'=', KEY_EQUAL, 0},
            {'/', KEY_SLASH, 0},
            {'.', KEY_DOT, 0},
            {',', KEY_COMMA, 0},
            {';', KEY_SEMICOLON, 0},
            {'\'', KEY_APOSTROPHE, 0},
            {'[', KEY_LEFTBRACE, 0},
            {']', KEY_RIGHTBRACE, 0},
            {'\\', KEY_BACKSLASH, 0},
            {'`', KEY_GRAVE, 0},
            {'\t', KEY_TAB, 0},
            {'_', KEY_MINUS, 1},
            {'+', KEY_EQUAL, 1},
            {'?', KEY_SLASH, 1},
            {'>', KEY_DOT, 1},
            {'<', KEY_COMMA, 1},
            {':', KEY_SEMICOLON, 1},
            {'"', KEY_APOSTROPHE, 1},
            {'{', KEY_LEFTBRACE, 1},
            {'}', KEY_RIGHTBRACE, 1},
            {'|', KEY_BACKSLASH, 1},
            {'~', KEY_GRAVE, 1},
            {'!', KEY_1, 1},
            {'@', KEY_2, 1},
            {'#', KEY_3, 1},
            {'$', KEY_4, 1},
            {'%', KEY_5, 1},
            {'^', KEY_6, 1},
            {'&', KEY_7, 1},
            {'*', KEY_8, 1},
            {'(', KEY_9, 1},
            {')', KEY_0, 1},
        };

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
            shift = true;
        } else {
            shift = false;
        }

        for (auto& e : table) {
            if (e.ch == c) {
                code = e.code;
                shift = shift || e.shift;

                return true;
            }
        }

        return false;
    }

    void controlIoCb(struct ev_loop*, ev_io* w, int);

    // no destructor: the subobjects registered after this in the caller's
    // pool die first — watcher stop, fd close, fifo unlink, then the impl
    struct ControlImpl: public Control {
        Composer* comp = nullptr;
        struct ev_loop* loop = nullptr;
        Renderer* renderer = nullptr;
        Scene* scene = nullptr;
        int* fd = nullptr;
        ev_io* io = nullptr;
        StringBuilder path;
        char line[1024] = "";
        size_t lineLen = 0;

        ControlImpl(Composer& c, StringView fifoPath);

        void handleLine(StringView cmd);
        void handleInput();
        void reopen();
        void dumpState(StringView outPath);
    };

    void controlIoCb(struct ev_loop*, ev_io* w, int) {
        ((ControlImpl*)w->data)->handleInput();
    }
}

ControlImpl::ControlImpl(Composer& c, StringView fifoPath)
    : comp(&c)
    , loop(c.loop)
    , renderer(c.renderer)
    , scene(c.scene)
{
    path << fifoPath;
    unlink(path.cStr());
    STD_VERIFY(mkfifo(path.cStr(), 0600) == 0);

    // registered first, runs last: the fd is closed before the fifo leaves
    // the filesystem
    ObjPool& pool = *c.pool;
    StringView stored = pool.intern(sv(path));

    pooledGuard(pool, [stored] {
        unlink(Buffer(stored).cStr());
    });

    fd = pooledFD(pool, -1);
    io = createEvIo(pool, loop);
    *fd = open(path.cStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    STD_VERIFY(*fd >= 0);

    ev_io_init(io, controlIoCb, *fd, EV_READ);
    io->data = this;
    ev_io_start(loop, io);
    *(comp->log) << "imway: control FIFO: "_sv << sv(path) << endL;
}

void ControlImpl::handleLine(StringView cmd) {
    StringView verb, args;

    if (!cmd.split(' ', verb, args)) {
        verb = cmd;
        args = {};
    }

    if (verb == "motion"_sv) {
        StringView xs, ys;

        if (args.split(' ', xs, ys)) {
            PointerMotionEvent ev;

            ev.x = parseFloat(xs);
            ev.y = parseFloat(ys);
            comp->entry->pointerMotion(ev);
        }
    } else if (verb == "button"_sv) {
        StringView which, state;

        if (args.split(' ', which, state)) {
            u32 btn = which == "left"_sv ? BTN_LEFT : which == "right"_sv ? BTN_RIGHT : BTN_MIDDLE;

            comp->entry->button(btn, state == "press"_sv);
        }
    } else if (verb == "key"_sv) {
        StringView code, state;

        if (args.split(' ', code, state)) {
            comp->entry->key((u32)code.stou(), state == "press"_sv);
        }
    } else if (verb == "relmotion"_sv) {
        StringView dxs, dys;

        if (args.split(' ', dxs, dys)) {
            PointerMotionEvent ev;

            ev.kind = PointerMotionKind::relative;
            ev.dx = ev.dxRaw = parseFloat(dxs);
            ev.dy = ev.dyRaw = parseFloat(dys);
            comp->entry->pointerMotion(ev);
        }
    } else if (verb == "swipe"_sv) {
        // swipe <begin N | update dx dy | end>
        StringView phase, rest;

        if (!args.split(' ', phase, rest)) {
            phase = args;
        }

        if (phase == "begin"_sv) {
            comp->entry->swipeBegin((u32)rest.stou());
        } else if (phase == "update"_sv) {
            StringView dxs, dys;

            if (rest.split(' ', dxs, dys)) {
                comp->entry->swipeUpdate(parseFloat(dxs), parseFloat(dys));
            }
        } else if (phase == "end"_sv) {
            comp->entry->swipeEnd(rest == "cancel"_sv);
        }
    } else if (verb == "pinch"_sv) {
        // pinch <begin N | update dx dy scale rot | end>
        StringView phase, rest;

        if (!args.split(' ', phase, rest)) {
            phase = args;
        }

        if (phase == "begin"_sv) {
            comp->entry->pinchBegin((u32)rest.stou());
        } else if (phase == "update"_sv) {
            StringView a, b, c, d, tmp;

            rest.split(' ', a, tmp);
            tmp.split(' ', b, tmp);
            tmp.split(' ', c, d);
            comp->entry->pinchUpdate(parseFloat(a), parseFloat(b), parseFloat(c), parseFloat(d));
        } else if (phase == "end"_sv) {
            comp->entry->pinchEnd(rest == "cancel"_sv);
        }
    } else if (verb == "hold"_sv) {
        // hold <begin N | end>
        StringView phase, rest;

        if (!args.split(' ', phase, rest)) {
            phase = args;
        }

        if (phase == "begin"_sv) {
            comp->entry->holdBegin((u32)rest.stou());
        } else if (phase == "end"_sv) {
            comp->entry->holdEnd(rest == "cancel"_sv);
        }
    } else if (verb == "type"_sv) {
        for (u8 c : args) {
            u32 kc;
            bool shift;

            if (!asciiToKey((char)c, kc, shift)) {
                continue;
            }

            if (shift) {
                comp->entry->key(KEY_LEFTSHIFT, true);
            }

            comp->entry->key(kc, true);
            comp->entry->key(kc, false);

            if (shift) {
                comp->entry->key(KEY_LEFTSHIFT, false);
            }
        }
    } else if (verb == "hscroll"_sv) {
        ScrollEvent ev;

        ev.dx = parseFloat(args);
        ev.discreteX = (i32)ev.dx;
        ev.source = ScrollSource::wheel;
        comp->entry->scroll(ev);
    } else if (verb == "scroll"_sv) {
        ScrollEvent ev;

        ev.dy = parseFloat(args);
        ev.discreteY = (i32)ev.dy;
        ev.value120Y = (i32)ev.dy * 120;
        ev.source = ScrollSource::wheel;
        comp->entry->scroll(ev);
    } else if (verb == "tablet"_sv) {
        // tablet <proximity_in|proximity_out|down|up|motion> <x> <y> [pressure]
        StringView phase, rest, xs, ys, ps;

        args.split(' ', phase, rest);

        TabletToolEvent ev;

        ev.phase = phase == "proximity_in"_sv ? TabletPhase::proximityIn : phase == "proximity_out"_sv ? TabletPhase::proximityOut : phase == "down"_sv ? TabletPhase::tipDown : phase == "up"_sv ? TabletPhase::tipUp : TabletPhase::motion;

        if (rest.split(' ', xs, ys)) {
            StringView yy;

            ys.split(' ', yy, ps);
            ev.x = parseFloat(xs);
            ev.y = parseFloat(yy.empty() ? ys : yy);

            if (!ps.empty()) {
                ev.pressureSet = true;
                ev.pressure = parseFloat(ps);
            }
        }

        comp->entry->tabletTool(ev);
    } else if (verb == "screenshot"_sv) {
        renderer->screenshot(args);
        *(comp->log) << "imway: screenshot by command: "_sv << args << endL;
    } else if (verb == "sdr-white"_sv) {
        comp->output->setSdrWhite(parseFloat(args));
    } else if (verb == "night"_sv) {
        comp->output->setColorTemp(parseFloat(args));
    } else if (verb == "dump"_sv) {
        dumpState(args);
    } else if (verb == "gpu-fatal"_sv) {
        // exercises the death policy end to end: the log line, the prompt
        // exit, no hang
        *(comp->log) << "imway: vulkan device lost, exiting"_sv << endL;
        exit(1);
    } else if (verb == "quit"_sv) {
        ev_break(loop, EVBREAK_ALL);
    } else {
        *(comp->log) << "imway: unknown command: "_sv << cmd << endL;
    }
}

// one line per entity, key=value fields, free-text (title) strictly last;
// written to <path>.tmp and renamed so the scenario can poll for the final
// path and read a complete file
void ControlImpl::dumpState(StringView outPath) {
    StringBuilder out;

    forEach<Toplevel>(scene->toplevels, [&](Toplevel& t) {
        Surface* s = t.surface.get();
        Icon* icon = t.icon(*comp);

        out << "toplevel id="_sv << t.id << " mapped="_sv << (int)t.mapped << " csd="_sv << (int)t.csd << " fullscreen="_sv << (int)t.fullscreen << " minimized="_sv << (int)t.minimized << " maximized="_sv << (int)t.maximized << " activated="_sv << (int)t.activated << " docked="_sv << (int)t.docked << " modal="_sv << (int)t.modal << " focused="_sv << (int)(scene->focusedToplevel.get() == &t) << " unresponsive="_sv << (int)t.unresponsive << " focus_seq="_sv << t.focusedAt << " x="_sv << (int)t.curX << " y="_sv << (int)t.curY << " w="_sv << (int)t.applyW << " h="_sv << (int)t.applyH;

        if (s) {
            out << " imgx="_sv << (int)s->imgX << " imgy="_sv << (int)s->imgY << " client_w="_sv << s->geomW() << " client_h="_sv << s->geomH() << " content_type="_sv << s->contentType << " tearing="_sv << (int)s->tearingAsync;
        }

        out << " parent="_sv << (t.parent ? t.parent->id : 0) << " icon_gen="_sv << (icon ? icon->gen : 0) << " tag="_sv << sv(t.tag) << " app_id="_sv << sv(t.appId) << " title="_sv << sv(t.title) << "\n"_sv;
    });

    forEach<Popup>(scene->popups, [&](Popup& p) {
        Surface* s = p.surface.get();

        out << "popup mapped="_sv << (int)p.mapped << " grab="_sv << (int)p.grab << " x="_sv << p.x << " y="_sv << p.y;

        if (s) {
            out << " imgx="_sv << (int)s->imgX << " imgy="_sv << (int)s->imgY << " w="_sv << s->viewW() << " h="_sv << s->viewH();
        }

        out << "\n"_sv;
    });

    out << "focus id="_sv << (scene->focusedToplevel ? scene->focusedToplevel->id : 0) << "\n"_sv;
    out << "layout "_sv << StringView(scene->layout) << "\n"_sv;
    out << "captured kb="_sv << (int)scene->kbCaptured << " ptr="_sv << (int)scene->ptrCaptured << "\n"_sv;
    out << "scanout candidate="_sv << scene->scanoutCandidateId << "\n"_sv;
    out << "bell count="_sv << scene->bellCount << "\n"_sv;
    out << "cursor shape="_sv << (int)scene->cursorShape << " surface="_sv << (int)(scene->cursorSurface != nullptr) << "\n"_sv;
    out << "ime popup="_sv << (int)(scene->imePopup.get() != nullptr) << " x="_sv << (int)scene->imePopupX << " y="_sv << (int)scene->imePopupY << "\n"_sv;

    const HdrOutputMetadata& metadata = comp->output->hdrMetadata();

    out << "hdr metadata="_sv << (int)metadata.hdr << " min="_sv << metadata.minNits << " max="_sv << metadata.maxNits << " max_cll="_sv << metadata.maxCll << " max_fall="_sv << metadata.maxFall << "\n"_sv;
    out << "color_intermediate_bytes="_sv << renderer->colorIntermediateBytes() << "\n"_sv;

    StringBuilder tmpPath;

    tmpPath << outPath << ".tmp"_sv;

    ScopedFD f(open(tmpPath.cStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

    if (f.get() < 0) {
        *(comp->log) << "imway: dump: cannot open "_sv << sv(tmpPath) << endL;

        return;
    }

    FDRegular w(f);

    w.write(out.data(), out.used());
    w.finish();
    STD_VERIFY(rename(tmpPath.cStr(), Buffer(outPath).cStr()) == 0);
}

void ControlImpl::handleInput() {
    char tmp[512];

    for (;;) {
        ssize_t n = read(*fd, tmp, sizeof tmp);

        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                if (tmp[i] == '\n') {
                    if (lineLen) {
                        handleLine({(const u8*)line, lineLen});
                    }

                    lineLen = 0;
                } else if (lineLen + 1 < sizeof(line)) {
                    line[lineLen++] = tmp[i];
                }
            }
        } else if (n == 0) {
            reopen();

            return;
        } else {
            return;
        }
    }
}

void ControlImpl::reopen() {
    ev_io_stop(loop, io);

    if (*fd >= 0) {
        close(*fd);
    }

    *fd = open(path.cStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (*fd < 0) {
        return;
    }

    ev_io_set(io, *fd, EV_READ);
    ev_io_start(loop, io);
}

Control* Control::create(Composer& c, StringView fifoPath) {
    return c.pool->make<ControlImpl>(c, fifoPath);
}
