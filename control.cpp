#include "control.h"

#include "log.h"
#include "icon.h"
#include "util.h"
#include "wifi.h"
#include "scene.h"
#include "output.h"
#include "pooled.h"
#include "composer.h"
#include "listener.h"
#include "keyboard.h"
#include "mixer.h"
#include "notifier.h"
#include "renderer.h"
#include "settings.h"
#include "wayland.h"
#include "imgui_wm.h"
#include "intr_list.h"
#include "dbus_menu.h"
#include "input_sink.h"
#include "chaos_monkey.h"
#include "kms_intercept.h"
#include "status_notifier.h"
#include "ev_watch.h"

#include <std/sys/fd.h>
#include <std/ios/sys.h>
#include <std/dbg/verify.h>
#include <std/ios/out_fd.h>
#include <std/mem/obj_pool.h>

#include <ev.h>
#include <fcntl.h>
#include <stdio.h> // rename(2) only
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/input-event-codes.h>

using namespace stl;

// present only in coverage-instrumented builds
extern "C" int __llvm_profile_write_file(void) __attribute__((weak));

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
        int* fd = nullptr;
        ev_io* io = nullptr;
        Buffer path;
        char line[1024] = "";
        size_t lineLen = 0;
        // the desired size the dump resolves toplevel icons at; the icon-size
        // verb drives it so a scenario can probe the size-bucket curve
        u32 dumpIconSize = 48;

        ControlImpl(Composer& c, StringView fifoPath);

        void handleLine(StringView cmd);
        bool kmsVerb(StringView verb, StringView args);
        void handleInput();
        void reopen();
        void dumpState(StringView outPath);
    };

    void controlIoCb(struct ev_loop*, ev_io* w, int) {
        ((ControlImpl*)w->data)->handleInput();
    }

    template <size_t N>
    void copyText(char (&out)[N], StringView value) {
        size_t length = value.length() < N - 1 ? value.length() : N - 1;

        memcpy(out, value.data(), length);
        out[length] = 0;
    }

    // a DBusMenu model as the compositor holds it: the menu, then its items
    // depth-first, the label (free text) last on each line
    static void dumpMenuItems(StringBuilder& out, const Vector<DBusMenuItem*>& items, int depth) {
        for (DBusMenuItem* item : items) {
            out << "menuitem depth="_sv << depth << " id="_sv << item->id << " enabled="_sv << (int)item->enabled << " visible="_sv << (int)item->visible << " separator="_sv << (int)item->separator << " toggle="_sv << (int)item->toggle << " state="_sv << item->toggleState << " submenu="_sv << (int)item->submenu << " disposition="_sv << (int)item->disposition << " icon_w="_sv << (item->iconData ? item->iconData->width : 0) << " icon_name="_sv << sv(item->iconName) << " shortcut="_sv << sv(item->shortcut) << " label="_sv << sv(item->label) << "\n"_sv;
            dumpMenuItems(out, item->children, depth + 1);
        }
    }

    static void dumpMenu(StringBuilder& out, StringView owner, const DBusMenu& menu) {
        out << "menu "_sv << owner << " ready="_sv << (int)menu.ready << " revision="_sv << menu.revision << " activation="_sv << (menu.hasActivationRequest ? menu.activationRequested : 0) << "\n"_sv;

        // the headings as the global menu bar last drew them, so a scenario
        // clicks one by its rectangle instead of guessing from the font
        for (DBusMenuItem* item : menu.items) {
            if (item->barRect[2] >= 0.f) {
                out << "menubar heading id="_sv << item->id << " x0="_sv << (int)item->barRect[0] << " y0="_sv << (int)item->barRect[1] << " x1="_sv << (int)item->barRect[2] << " y1="_sv << (int)item->barRect[3] << " label="_sv << sv(item->label) << "\n"_sv;
            }
        }

        dumpMenuItems(out, menu.items, 0);
    }
}

ControlImpl::ControlImpl(Composer& c, StringView fifoPath)
    : comp(&c)
{
    path.append(fifoPath.data(), fifoPath.length());
    unlink(path.cStr());
    STD_VERIFY(mkfifo(path.cStr(), 0600) == 0);

    // registered first, runs last: the fd is closed before the fifo leaves
    // the filesystem
    ObjPool& pool = *c.pool;
    StringView stored = pool.intern(sv(path));

    pooledGuard(pool, [stored] {
        unlink(Buffer(stored).cStr());
    });

    fd = pool.make<int>(-1);
    int* heldFd = fd;

    pooledGuard(pool, [heldFd] {
        if (*heldFd >= 0) {
            close(*heldFd);
        }
    });

    io = pool.make<ev_io>();
    struct ev_loop* heldLoop = comp->loop;
    ev_io* heldIo = io;

    pooledGuard(pool, [heldLoop, heldIo] {
        if (ev_is_active(heldIo)) {
            ev_io_stop(heldLoop, heldIo);
        }
    });

    *fd = c.chaos->controlOpen(open(path.cStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    STD_VERIFY(*fd >= 0);

    evIoInit(io, controlIoCb, *fd, EV_READ);
    io->data = this;
    ev_io_start(comp->loop, io);
    *(comp->log) << "imway: control FIFO: "_sv << sv(path) << endL;
}

// the fake KMS device's verbs; false for any other
bool ControlImpl::kmsVerb(StringView verb, StringView args) {
    if (verb == "kms-connector"_sv) {
        // flip the fake connector and re-probe, like a udev hotplug would
        comp->kmsIntercept->setConnected((int)args.stou());
        comp->output->hotplug();
        comp->scene->needsFrame = true;
    } else if (verb == "kms-fail-commit"_sv) {
        StringView err, rest, count, testToo;

        if (args.split(' ', err, rest)) {
            if (!rest.split(' ', count, testToo)) {
                count = rest;
                testToo = "0"_sv;
            }

            comp->kmsIntercept->failCommits((int)err.stou(), (int)count.stou(), testToo.stou() != 0);
        }
    } else if (verb == "kms-fail-new-fb"_sv) {
        comp->kmsIntercept->failNewFb((int)args.stou());
    } else if (verb == "kms-fail-prime"_sv) {
        StringView err, rest, count, skip;

        if (args.split(' ', err, rest)) {
            if (!rest.split(' ', count, skip)) {
                count = rest;
                skip = "0"_sv;
            }

            comp->kmsIntercept->failPrime((int)err.stou(), (int)count.stou(), (int)skip.stou());
        }
    } else if (verb == "kms-fail-addfb"_sv) {
        StringView err, count;

        if (args.split(' ', err, count)) {
            comp->kmsIntercept->failAddFb((int)err.stou(), (int)count.stou());
        }
    } else if (verb == "kms-reject-cursor"_sv) {
        comp->kmsIntercept->rejectCursor((int)args.stou());
        comp->scene->needsFrame = true;
    } else if (verb == "kms-modes"_sv) {
        comp->kmsIntercept->setModes((int)args.stou());
    } else if (verb == "kms-hold-flips"_sv) {
        comp->kmsIntercept->holdFlips(args.stou() != 0);
    } else if (verb == "kms-lease-fault"_sv) {
        comp->kmsIntercept->leaseFault((int)args.stou());
    } else if (verb == "kms-fail-lookup"_sv) {
        comp->kmsIntercept->failLookups(args);
    } else {
        return false;
    }

    return true;
}

void ControlImpl::handleLine(StringView cmd) {
    // every command acts on the state the clients have already produced,
    // never on what the loop happened to have read by now
    comp->wayland->drainClients();

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
            // the side buttons of a five-button mouse reach clients that
            // map them (the screenshot editor's plt binding) as well
            u32 btn = which == "left"_sv ? BTN_LEFT : which == "right"_sv ? BTN_RIGHT : which == "side"_sv ? BTN_SIDE : which == "extra"_sv ? BTN_EXTRA : which == "forward"_sv ? BTN_FORWARD : BTN_MIDDLE;

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
    } else if (verb == "hscroll"_sv || verb == "scroll"_sv) {
        // scroll|hscroll <notches|stop> [wheel|finger|continuous]
        bool horizontal = verb == "hscroll"_sv;
        StringView amount, kind;

        if (!args.split(' ', amount, kind)) {
            amount = args;
            kind = {};
        }

        ScrollEvent ev;

        ev.source = kind == "finger"_sv ? ScrollSource::finger : kind == "continuous"_sv ? ScrollSource::continuous : ScrollSource::wheel;

        if (amount == "stop"_sv) {
            ev.stopX = horizontal;
            ev.stopY = !horizontal;
        } else if (horizontal) {
            ev.dx = parseFloat(amount);
            ev.discreteX = (i32)ev.dx;
            ev.value120X = (i32)ev.dx * 120;
        } else {
            ev.dy = parseFloat(amount);
            ev.discreteY = (i32)ev.dy;
            ev.value120Y = (i32)ev.dy * 120;
        }

        comp->entry->scroll(ev);
    } else if (verb == "tablet"_sv) {
        // tablet <proximity_in|proximity_out|down|up|motion> <x> <y> [axis ...]
        // An axis is name=value, or a bare number for the pressure, which is
        // how this command started out. tilt, wheel and button take a pair:
        // tilt=<x>,<y>  wheel=<degrees>,<clicks>  button=<code>,<press|release>
        StringView phase, rest, xs, ys;

        args.split(' ', phase, rest);

        TabletToolEvent ev;

        ev.phase = phase == "proximity_in"_sv ? TabletPhase::proximityIn : phase == "proximity_out"_sv ? TabletPhase::proximityOut : phase == "down"_sv ? TabletPhase::tipDown : phase == "up"_sv ? TabletPhase::tipUp : TabletPhase::motion;

        if (rest.split(' ', xs, ys)) {
            StringView yy, tail;

            if (!ys.split(' ', yy, tail)) {
                yy = ys;
                tail = {};
            }

            ev.x = parseFloat(xs);
            ev.y = parseFloat(yy);

            while (!tail.empty()) {
                StringView token, more;

                if (!tail.split(' ', token, more)) {
                    token = tail;
                    more = {};
                }

                tail = more;

                StringView key, value;

                if (!token.split('=', key, value)) {
                    ev.pressureSet = true;
                    ev.pressure = parseFloat(token);

                    continue;
                }

                StringView first, second;
                bool pair = value.split(',', first, second);

                if (key == "pressure"_sv) {
                    ev.pressureSet = true;
                    ev.pressure = parseFloat(value);
                } else if (key == "distance"_sv) {
                    ev.distanceSet = true;
                    ev.distance = parseFloat(value);
                } else if (key == "rotation"_sv) {
                    ev.rotationSet = true;
                    ev.rotation = parseFloat(value);
                } else if (key == "slider"_sv) {
                    ev.sliderSet = true;
                    ev.slider = parseFloat(value);
                } else if (key == "tilt"_sv && pair) {
                    ev.tiltSet = true;
                    ev.tiltX = parseFloat(first);
                    ev.tiltY = parseFloat(second);
                } else if (key == "wheel"_sv && pair) {
                    ev.wheelSet = true;
                    ev.wheelDegrees = parseFloat(first);
                    ev.wheelClicks = (i32)second.stou();
                } else if (key == "button"_sv && pair) {
                    ev.buttonSet = true;
                    ev.button = (u32)first.stou();
                    ev.buttonPressed = second == "press"_sv;
                }
            }
        }

        comp->entry->tabletTool(ev);
    } else if (verb == "frame"_sv) {
        // a composed frame of everything sent before, without a readback:
        // the barrier a scenario needs before input that must meet it
        comp->renderer->composeNow();
    } else if (verb == "screenshot"_sv) {
        comp->renderer->screenshot(args, false);
        *(comp->log) << "imway: screenshot by command: "_sv << args << endL;
    } else if (verb == "screenshot-raw"_sv) {
        comp->renderer->screenshot(args, true);
        *(comp->log) << "imway: raw screenshot by command: "_sv << args << endL;
    } else if (verb == "sdr-white"_sv) {
        comp->output->setSdrWhite(parseFloat(args));
    } else if (verb == "night"_sv) {
        comp->output->setColorTemp(parseFloat(args));
    } else if (verb == "chaos"_sv) {
        // faults armed once the scenario reached the state they break; a
        // frame follows, for the faults a frame spends
        comp->chaos->arm(args);
        comp->scene->needsFrame = true;
        *(comp->log) << "imway: control: chaos "_sv << args << endL;
    } else if (verb == "dump"_sv) {
        dumpState(args);
    } else if (verb == "icon-size"_sv) {
        dumpIconSize = (u32)args.stou();
    } else if (comp->kmsIntercept && kmsVerb(verb, args)) {
        // the fake KMS device's verbs: a compositor without one has none
    } else if (verb == "session"_sv) {
        // fires the same listener lists a libseat VT switch would
        if (args.stou() != 0) {
            forEach<Listener>(comp->sessionEnabledListeners, [](Listener& l) {
                l.onListen();
            });
        } else {
            forEach<Listener>(comp->sessionDisabledListeners, [](Listener& l) {
                l.onListen();
            });
        }

        comp->scene->needsFrame = true;
    } else if (verb == "render-fault"_sv) {
        // injects a renderer-attributed client fault: the real producers
        // (device OOM on a client-sized texture) cannot fire in a scenario
        comp->scene->renderFaults.pushBack(args.stou());
        comp->scene->needsFrame = true;
    } else if (verb == "gpu-fatal"_sv) {
        // exercises the death policy end to end: the log line, the prompt
        // exit, no hang
        *(comp->log) << "imway: vulkan device lost, exiting"_sv << endL;
        // _exit, not exit: the gpu is gone, and unwinding through atexit
        // (the driver's own, the profile writer's) while the copy thread and
        // the driver's threads are still live is how this died with SIGSEGV
        // instead of its exit code under an instrumented build. The
        // counters are written by hand: what this session ran is measured
        // like any other's
        if (__llvm_profile_write_file) {
            __llvm_profile_write_file();
        }

        _exit(1);
    } else if (verb == "rule"_sv) {
        // `rule INDEX POLICY APP`: a per-application notification rule, which
        // the settings dialog otherwise owns
        StringView index, policy, app, rest;

        if (args.split(' ', index, rest) && rest.split(' ', policy, app)) {
            size_t slot = (size_t)index.stou();

            if (slot < Settings::notificationRuleCapacity) {
                NotificationRule value;

                copyText(value.app, app);
                value.policy = (NotificationPolicy)policy.stou();
                comp->settings->setNotificationRule(slot, value);

                if (comp->settings->notificationRuleCount() <= slot) {
                    comp->settings->setNotificationRuleCount(slot + 1);
                }

                *(comp->log) << "imway: control: rule "_sv << index << endL;
            }
        }
    } else if (verb == "notify"_sv) {
        // `notify APP REPLACES CRITICAL SUMMARY`: the internal producers'
        // entry point, so a scenario can drive the notifier without a bus
        StringView app, replaces, critical, summary, rest;

        if (args.split(' ', app, rest) && rest.split(' ', replaces, rest) && rest.split(' ', critical, summary)) {
            Post p;

            p.app = app;
            p.summary = summary;
            p.body = "from the control fifo"_sv;
            p.critical = critical == "1"_sv;
            p.replacesId = (u32)replaces.stou();

            *(comp->log) << "imway: control: notification "_sv << comp->notifier->post(p) << endL;
        }

        comp->scene->needsFrame = true;
    } else if (verb == "set"_sv) {
        StringView key, value;

        if (!args.split(' ', key, value)) {
            key = args;
            value = {};
        }

        if (applySettingText(*comp->settings, key, value)) {
            *(comp->log) << "imway: control: set "_sv << key << endL;
        } else {
            *(comp->log) << "imway: control: unknown setting "_sv << key << endL;
        }

        comp->scene->needsFrame = true;
    } else if (verb == "quit"_sv) {
        ev_break(comp->loop, EVBREAK_ALL);
    } else {
        *(comp->log) << "imway: unknown command: "_sv << cmd << endL;
    }
}

// one line per entity, key=value fields, free-text (title) strictly last;
// written to <path>.tmp and renamed so the scenario can poll for the final
// path and read a complete file
void ControlImpl::dumpState(StringView outPath) {
    if (outPath.empty()) {
        *(comp->log) << "imway: dump: no path"_sv << endL;

        return;
    }

    Buffer output;
    StringBuilder out((Buffer&&)output);

    forEach<Toplevel>(comp->scene->toplevels, [&](Toplevel& t) {
        Surface* s = t.surface.get();
        Icon* icon = t.icon(*comp, dumpIconSize);

        out << "toplevel id="_sv << t.id << " mapped="_sv << (int)t.mapped << " csd="_sv << (int)t.csd << " fullscreen="_sv << (int)t.fullscreen << " minimized="_sv << (int)t.minimized << " maximized="_sv << (int)t.maximized << " activated="_sv << (int)t.activated << " docked="_sv << (int)t.docked << " modal="_sv << (int)t.modal << " focused="_sv << (int)(comp->scene->focusedToplevel.get() == &t) << " unresponsive="_sv << (int)t.unresponsive << " focus_seq="_sv << t.focusedAt << " x="_sv << (int)t.curX << " y="_sv << (int)t.curY << " w="_sv << (int)t.applyW << " h="_sv << (int)t.applyH;

        if (s) {
            out << " imgx="_sv << (int)s->imgX << " imgy="_sv << (int)s->imgY << " client_w="_sv << s->geomW() << " client_h="_sv << s->geomH() << " content_type="_sv << s->contentType << " tearing="_sv << (int)s->tearingAsync;
        }

        out << " parent="_sv << (t.parent ? t.parent->id : 0) << " icon_gen="_sv << (icon ? icon->gen : 0) << " icon_w="_sv << (icon ? icon->width : 0) << " tag="_sv << sv(t.tag) << " app_id="_sv << sv(t.appId) << " title="_sv << sv(t.title) << "\n"_sv;
    });

    forEach<Popup>(comp->scene->popups, [&](Popup& p) {
        Surface* s = p.surface.get();

        out << "popup mapped="_sv << (int)p.mapped << " grab="_sv << (int)p.grab << " x="_sv << p.x << " y="_sv << p.y;

        if (s) {
            out << " imgx="_sv << (int)s->imgX << " imgy="_sv << (int)s->imgY << " w="_sv << s->viewW() << " h="_sv << s->viewH();
        }

        out << "\n"_sv;
    });

    // the compositor's own ImGui windows drawn last frame, so a scenario
    // can aim clicks at a dialog from its rectangle; the renderer created
    // the context in its constructor, before the control FIFO
    ImGuiContext* g = ImGui::GetCurrentContext();

    // which of them holds the keyboard, and whether a text field is
    // taking input: a dialog that lost this cannot be typed into
    out << "imgui focus name="_sv << StringView(g->NavWindow ? g->NavWindow->Name : "-") << " want_text="_sv << (int)ImGui::GetIO().WantTextInput << " active_id="_sv << (int)(g->ActiveId != 0) << "\n"_sv;

    // the text the active field holds: typed characters trickle in one
    // frame at a time, so a scenario presses Enter only once they are all
    // there
    if (g->ActiveId && g->InputTextState.ID == g->ActiveId) {
        out << "imgui input len="_sv << (i64)g->InputTextState.TextLen << " text="_sv << StringView((const u8*)g->InputTextState.TextA.Data, (size_t)g->InputTextState.TextLen) << "\n"_sv;
    }

    for (ImGuiWindow* w : g->Windows) {
        if (w->WasActive && !w->Hidden && !(w->Flags & ImGuiWindowFlags_ChildWindow)) {
            out << "imgui name="_sv << StringView(w->Name) << " x="_sv << (int)w->Pos.x << " y="_sv << (int)w->Pos.y << " w="_sv << (int)w->Size.x << " h="_sv << (int)w->Size.y << "\n"_sv;
        }
    }

    int activeToasts = 0;
    int keptToasts = 0;

    comp->notifier->active([&activeToasts](Toast&) {
        activeToasts++;
    });
    comp->notifier->history([&keptToasts](Toast&) {
        keptToasts++;
    });

    out << "notifications active="_sv << activeToasts << " history="_sv << keptToasts << "\n"_sv;

    // the session-bus peers' models: each window's global menu, each tray
    // item (its pixmap as the dock resolves it) and its menu
    forEach<Toplevel>(comp->scene->toplevels, [&](Toplevel& t) {
        Surface* s = t.surface.get();

        if (s && s->appMenu) {
            auto& owner = sb();

            owner << "appmenu="_sv << t.id;
            dumpMenu(out, sv(owner), *s->appMenu);
        }
    });

    if (comp->statusNotifier) {
        comp->statusNotifier->items([&](StatusNotifierItem& item) {
            Icon* pixmap = comp->findIcon(item.iconSym, 16);
            Icon* attention = comp->findIcon(item.attentionIconSym, 16);

            out << "tray id="_sv << sv(item.id) << " status="_sv << sv(item.status) << " desktop="_sv << sv(item.desktopEntry) << " icon_name="_sv << sv(item.iconName) << " attention_name="_sv << sv(item.attentionIconName) << " pixmap_w="_sv << (pixmap ? pixmap->width : 0) << " attention_w="_sv << (attention ? attention->width : 0) << " menu="_sv << (int)item.hasMenu << " item_is_menu="_sv << (int)item.itemIsMenu << " title="_sv << sv(item.title) << "\n"_sv;

            if (item.menu) {
                dumpMenu(out, "tray="_sv, *item.menu);
            }
        });
    }

    if (comp->wifi) {
        int networks = 0;

        comp->wifi->networks([&networks](WifiNetwork&) {
            networks++;
        });
        out << "wifi state="_sv << (int)comp->wifi->state() << " networks="_sv << networks << " passphrase="_sv << (int)comp->wifi->passphraseWanted() << "\n"_sv;

        comp->wifi->networks([&](WifiNetwork& n) {
            out << "wifinet strength="_sv << n.strength << " connected="_sv << (int)n.connected << " known="_sv << (int)n.known << " type="_sv << sv(n.type) << " path="_sv << sv(n.path) << " name="_sv << sv(n.name) << "\n"_sv;
        });
    }
    out << "bar app_id="_sv << StringView(comp->scene->barAppId[0] ? comp->scene->barAppId : "-") << "\n"_sv;
    out << "battery pct="_sv << (i64)comp->scene->batteryPct << " discharging="_sv << (int)comp->scene->batteryDischarging << "\n"_sv;
    // the level the volume keys step from, in whole percent
    if (comp->mixer) {
        out << "mixer volume="_sv << (i64)(comp->mixer->volume() * 100.f + .5f) << " muted="_sv << (int)comp->mixer->muted() << "\n"_sv;
    }
    out << "wifi glyph x0="_sv << (int)comp->scene->wifiGlyph[0] << " y0="_sv << (int)comp->scene->wifiGlyph[1] << " x1="_sv << (int)comp->scene->wifiGlyph[2] << " y1="_sv << (int)comp->scene->wifiGlyph[3] << "\n"_sv;
    out << "focus id="_sv << (comp->scene->focusedToplevel ? comp->scene->focusedToplevel->id : 0) << "\n"_sv;
    // the cached indicator and the live xkb group: they are refreshed on
    // different events, so a scenario can tell a stale indicator from a
    // group that really did not move
    out << "layout "_sv << StringView(comp->scene->layout) << " group="_sv << (int)comp->kb->activeLayout() << " count="_sv << (int)comp->kb->layoutCount() << "\n"_sv;
    out << "captured kb="_sv << (int)comp->scene->kbCaptured << " ptr="_sv << (int)comp->scene->ptrCaptured << "\n"_sv;
    // the input devices the settings know, one entry each as it is added
    out << "input devices="_sv << (u64)comp->settings->inputDeviceCount() << "\n"_sv;
    out << "scanout candidate="_sv << comp->scene->scanoutCandidateId << "\n"_sv;
    out << "bell count="_sv << comp->scene->bellCount << "\n"_sv;
    // the millisecond clock as the bell and the OSD read it
    out << "clock ms="_sv << comp->chaos->clockMs(nowMsec()) << "\n"_sv;
    out << "frames done="_sv << comp->scene->framesDone << "\n"_sv;

    if (comp->kmsIntercept) {
        // delivered page-flip events: the fake device's ground truth for
        // frames that actually reached the screen
        out << "kms flips="_sv << comp->kmsIntercept->flips() << " fbs="_sv << comp->kmsIntercept->liveFbs() << " gems="_sv << comp->kmsIntercept->liveGems() << "\n"_sv;
    }
    out << "cursor shape="_sv << (int)comp->scene->cursorShape << " surface="_sv << (int)(comp->scene->cursorSurface != nullptr) << " drawn="_sv << (int)comp->scene->cursorDrawn << "\n"_sv;
    out << "ime popup="_sv << (int)(comp->scene->imePopup.get() != nullptr) << " x="_sv << (int)comp->scene->imePopupX << " y="_sv << (int)comp->scene->imePopupY << "\n"_sv;

    const HdrOutputMetadata& metadata = comp->output->hdrMetadata();

    out << "hdr metadata="_sv << (int)metadata.hdr << " min="_sv << metadata.minNits << " max="_sv << metadata.maxNits << " max_cll="_sv << metadata.maxCll << " max_fall="_sv << metadata.maxFall << "\n"_sv;
    out << "color_intermediate_bytes="_sv << comp->renderer->colorIntermediateBytes() << "\n"_sv;

    out.xchg(output);
    Buffer tmpPath;
    StringBuilder pathBuilder((Buffer&&)tmpPath);

    pathBuilder << outPath << ".tmp"_sv;
    pathBuilder.xchg(tmpPath);

    ScopedFD f(open(tmpPath.cStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

    if (f.get() < 0) {
        *(comp->log) << "imway: dump: cannot open "_sv << sv(tmpPath) << endL;

        return;
    }

    FDRegular w(f);

    w.write(output.data(), output.used());
    w.finish();

    // the path came over the control FIFO — external input never gets to
    // take the session down
    // the temporary goes before the line that reports it: a reader of the
    // log finds it gone
    if (rename(tmpPath.cStr(), Buffer(outPath).cStr()) != 0) {
        unlink(tmpPath.cStr());
        *(comp->log) << "imway: dump: cannot rename "_sv << sv(tmpPath) << " to "_sv << outPath << endL;
    }
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
    ev_io_stop(comp->loop, io);

    if (*fd >= 0) {
        close(*fd);
    }

    *fd = open(path.cStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (*fd < 0) {
        return;
    }

    evIoSet(io, *fd, EV_READ);
    ev_io_start(comp->loop, io);
}

Control* Control::create(Composer& c, StringView fifoPath) {
    return c.pool->make<ControlImpl>(c, fifoPath);
}
