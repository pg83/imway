#include "composer.h"
#include "control.h"
#include "dbus_conn.h"
#include "notifications.h"
#include "notifier.h"
#include "device.h"
#include "device_headless.h"
#include "device_kms.h"
#include "input.h"
#include "input_sink.h"
#include "keyboard.h"
#include "mixer.h"
#include "wifi.h"
#include "output.h"
#include "icon_pool.h"
#include "icon_store.h"
#include "renderer.h"
#include "scene.h"
#include "session.h"
#include "util.h"
#include "wayland.h"

#include <stdlib.h>
#include <unistd.h>

#include <ev.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    void usage(const char* argv0) {
        sysE << "usage: "_sv << argv0 << " [--device auto|headless|/dev/dri/cardN] [--output NAME] [--mode WxH@HZ]" " [--socket NAME] [--xkb-layout L] [--xkb-options O] [--font PATH] [--scale K]" " [--frames N] [--screenshot PATH] [--control FIFO] [--dpms SEC] [--hdr SDR_WHITE_NITS] [--list] [-- CMD ARG...]"_sv << endL;
    }

    void childCb(struct ev_loop* loop, ev_child* w, int) {
        ev_child_stop(loop, w);
        ev_break(loop, EVBREAK_ALL);
    }
}

int main(int argc, char** argv) {
    StringView devicePath = "auto";
    StringView outputName;
    StringView modeStr;
    StringView socketName = "imway-0";
    StringView xkbLayout = "us,ru";
    StringView xkbOptions = "grp:caps_toggle";
    StringView fontPath;
    float uiScale = 1.f;
    StringView screenshotPath;
    StringView controlPath;
    int framesLimit = 0;
    double dpmsSec = 0;
    double hdrNits = 0;
    char** cmdArgv = nullptr;

    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> StringView {
            if (i + 1 >= argc) {
                usage(argv[0]);
                exit(2);
            }

            return argv[++i];
        };

        StringView arg(argv[i]);

        if (arg == "--socket"_sv) {
            socketName = next();
        } else if (arg == "--device"_sv) {
            devicePath = next();
        } else if (arg == "--output"_sv) {
            outputName = next();
        } else if (arg == "--mode"_sv) {
            modeStr = next();
        } else if (arg == "--xkb-layout"_sv) {
            xkbLayout = next();
        } else if (arg == "--xkb-options"_sv) {
            xkbOptions = next();
        } else if (arg == "--font"_sv) {
            fontPath = next();
        } else if (arg == "--scale"_sv) {
            uiScale = (float)parseFloat(StringView(next()));

            if (!(uiScale > 0.f)) {
                usage(argv[0]);

                return 2;
            }
        } else if (arg == "--frames"_sv) {
            framesLimit = (int)StringView(next()).stou();
        } else if (arg == "--dpms"_sv) {
            dpmsSec = parseFloat(StringView(next()));
        } else if (arg == "--hdr"_sv) {
            hdrNits = parseFloat(StringView(next()));
        } else if (arg == "--screenshot"_sv) {
            screenshotPath = next();
        } else if (arg == "--control"_sv) {
            controlPath = next();
        } else if (arg == "--"_sv) {
            if (i + 1 >= argc) {
                usage(argv[0]);

                return 2;
            }

            cmdArgv = argv + i + 1;

            break;
        } else if (arg == "--list"_sv) {
            DeviceKms::list();

            return 0;
        } else {
            usage(argv[0]);

            return 2;
        }
    }

    bool kms = devicePath != "headless"_sv;

    if (!getenv("XDG_RUNTIME_DIR")) {
        sysE << "XDG_RUNTIME_DIR is not set"_sv << endL;

        return 1;
    }

    ObjPool::Ref pool = ObjPool::fromMemory();
    struct ev_loop* loop = ev_default_loop(0);

    try {
        auto* scene = pool->make<Scene>();

        Composer c;

        c.pool = pool.mutPtr();
        c.loop = loop;
        c.scene = scene;

        Session* session = nullptr;

        if (kms) {
            try {
                session = Session::create(pool.mutPtr(), loop);
                sysO << "imway: libseat session on "_sv << session->seatName() << endL;
            } catch (...) {
                sysE << "imway: "_sv << Exception::current() << ", opening devices directly"_sv << endL;
                session = Session::createDirect(pool.mutPtr());
            }
        }

        c.session = session;

        Device* device = kms ? DeviceKms::create(pool.mutPtr(), loop, *session, devicePath == "auto"_sv ? StringView{} : devicePath) : DeviceHeadless::create(pool.mutPtr(), loop);

        ::Output* output = device->createOutput(outputName, modeStr, hdrNits);

        c.output = output;

        scene->outW = output->width();
        scene->outH = output->height();
        scene->hz = output->refresh();
        scene->drawCursor = kms || getenv("IMWAY_FORCE_CURSOR");
        scene->socketName = socketName;

        STD_VERIFY(output->start());

        Vector<DmabufFormat> formats;

        device->dmabufFormats([&formats](const DmabufFormat& f) {
            formats.pushBack(f);
        });

        Keyboard* kb = Keyboard::create(pool.mutPtr(), xkbLayout, xkbOptions);

        c.kb = kb;

        WaylandConfig wcfg;

        wcfg.socketName = socketName;
        wcfg.formats = formats.data();
        wcfg.formatCount = formats.length();
        wcfg.mainDevice = device->renderDevice();
        wcfg.output = output;
        wcfg.dpmsSec = kms ? dpmsSec : 0;
        wcfg.drmFd = device->drmFd();

        c.iconPool = IconPool::create(pool.mutPtr());
        c.icons = IconStore::create(c);

        // the notifier store always exists (internal senders like wifi
        // post to it regardless of a bus); the dbus service is layered on
        // top only when the session bus is reachable
        c.notifier = Notifier::create(c);
        c.bus = DBusConn::create(pool.mutPtr(), loop, false);

        if (c.bus) {
            c.notes = Notifications::create(c);
        }

        c.mixer = Mixer::create(c);

        c.sysbus = DBusConn::create(pool.mutPtr(), loop, true);
        c.wifi = c.sysbus ? Wifi::create(c) : nullptr;

        Wayland* wayland = Wayland::create(c, wcfg);

        c.wayland = wayland;

        if (c.bus) {
            // the socket is bound by now: dbus activation learns the real
            // name instead of a wrapper-script guess
            c.bus->setActivationEnv("WAYLAND_DISPLAY"_sv, socketName);
            c.bus->setActivationEnv("XDG_CURRENT_DESKTOP"_sv, "imway"_sv);
        }

        Renderer* renderer = device->createRenderer(c, fontPath, uiScale, framesLimit);

        c.renderer = renderer;

        if (session) {
            session->addListener(wayland->sessionListener());
        }

        if (kms) {
            try {
                InputSource::createLibinput(c);
            } catch (...) {
                sysE << "imway: no input, mouse is dead: "_sv << Exception::current() << endL;
            }
        }

        if (!controlPath.empty()) {
            Control::create(c, controlPath);
        }

        ev_child child;

        if (cmdArgv) {
            pid_t pid = fork();

            STD_VERIFY(pid >= 0);

            if (pid == 0) {
                const char* cmd = cmdArgv[0];

                setenv("WAYLAND_DISPLAY", Buffer(socketName).cStr(), 1);
                execvp(cmd, cmdArgv);

                try {
                    Errno().raise(StringBuilder() << "imway: exec "_sv << cmd);
                } catch (...) {
                    sysE << Exception::current() << endL;
                }

                _exit(127);
            }

            // the compositor lives as long as the command does
            ev_child_init(&child, childCb, pid, 0);
            ev_child_start(loop, &child);
        }

        wayland->run();

        if (!screenshotPath.empty()) {
            renderer->screenshot(screenshotPath);
            sysO << "imway: screenshot: "_sv << screenshotPath << endL;
        }

        sysO << "imway: clean exit after "_sv << scene->framesDone << " frames"_sv << endL;
    } catch (...) {
        sysE << "imway: fatal: "_sv << Exception::current() << endL;

        return 1;
    }

    return 0;
}
