#include "control.h"
#include "device.h"
#include "input.h"
#include "input_sink.h"
#include "output.h"
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
        sysE << "usage: "_sv << argv0 << " [--device auto|headless|/dev/dri/cardN] [--output NAME] [--mode WxH@HZ]" " [--socket NAME] [--xkb-layout L] [--xkb-options O] [--font PATH] [--scale K]" " [--frames N] [--screenshot PATH] [--control FIFO] [--list] [-- CMD ARG...]"_sv << endL;
    }

    void childCb(struct ev_loop* loop, ev_child* w, int) {
        ev_child_stop(loop, w);
        ev_break(loop, EVBREAK_ALL);
    }
}

int main(int argc, char** argv) {
    const char* devicePath = "auto";
    const char* outputName = nullptr;
    const char* modeStr = nullptr;
    const char* socketName = "imway-0";
    const char* xkbLayout = nullptr;
    const char* xkbOptions = nullptr;
    const char* fontPath = nullptr;
    float uiScale = 1.f;
    const char* screenshotPath = nullptr;
    const char* controlPath = nullptr;
    int framesLimit = 0;
    char** cmdArgv = nullptr;

    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> const char* {
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
            Device::list();

            return 0;
        } else {
            usage(argv[0]);

            return 2;
        }
    }

    bool kms = StringView(devicePath) != "headless"_sv;

    if (!getenv("XDG_RUNTIME_DIR")) {
        sysE << "XDG_RUNTIME_DIR is not set"_sv << endL;

        return 1;
    }

    ObjPool::Ref pool = ObjPool::fromMemory();
    struct ev_loop* loop = ev_default_loop(0);

    try {
        auto* scene = pool->make<Scene>();

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

        Device* device = kms ? Device::createKms(pool.mutPtr(), loop, *session, StringView(devicePath) == "auto"_sv ? nullptr : devicePath) : Device::createHeadless(pool.mutPtr(), loop);

        ::Output* output = device->createOutput(outputName, modeStr);

        scene->outW = output->width();
        scene->outH = output->height();
        scene->hz = output->refresh();
        scene->drawCursor = kms;

        STD_VERIFY(output->start());

        Vector<DmabufFormat> formats;

        for (size_t i = 0; i < device->dmabufFormatCount(); i++) {
            formats.pushBack(device->dmabufFormat(i));
        }

        WaylandConfig wcfg;

        wcfg.socketName = socketName;
        wcfg.xkbLayout = xkbLayout;
        wcfg.xkbOptions = xkbOptions;
        wcfg.formats = formats.data();
        wcfg.formatCount = formats.length();
        wcfg.mainDevice = device->renderDevice();

        Wayland* wayland = Wayland::create(pool.mutPtr(), loop, *scene, wcfg);

        Renderer* renderer = device->createRenderer(*scene, *output, *wayland->frameListener(), fontPath, uiScale, framesLimit);

        if (session) {
            session->addListener(wayland->sessionListener());
        }

        InputSink* sink = InputSink::tee(pool.mutPtr(), *renderer->sink(), *wayland->sink());

        if (kms) {
            try {
                InputSource::createLibinput(pool.mutPtr(), loop, *session, *sink, scene->outW, scene->outH);
            } catch (...) {
                sysE << "imway: no input, mouse is dead: "_sv << Exception::current() << endL;
            }
        }

        if (controlPath) {
            Control::create(pool.mutPtr(), loop, *sink, *renderer, controlPath);
        }

        ev_child child;

        if (cmdArgv) {
            pid_t pid = fork();

            STD_VERIFY(pid >= 0);

            if (pid == 0) {
                const char* cmd = cmdArgv[0];

                setenv("WAYLAND_DISPLAY", socketName, 1);
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

        if (screenshotPath) {
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
