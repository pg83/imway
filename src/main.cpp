#include "control.h"
#include "input.h"
#include "input_sink.h"
#include "output.h"
#include "renderer.h"
#include "scene.h"
#include "util.h"
#include "wayland.h"

#include <stdlib.h>
#include <string.h>

#include <ev.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    void usage(const char* argv0) {
        sysE << "usage: "_sv << argv0 << " [--backend headless|kms] [--drm-device PATH] [--socket NAME]" " [--size WxH] [--hz N] [--frames N] [--screenshot PATH] [--control FIFO]"_sv << endL;
    }

    bool parseSize(const char* s, int& w, int& h) {
        StringView v(s);
        StringView before, after;

        if (!v.split('x', before, after) || before.empty() || after.empty()) {
            return false;
        }

        w = (int)before.stou();
        h = (int)after.stou();

        return w > 0 && h > 0;
    }
}

int main(int argc, char** argv) {
    bool kms = false;
    const char* drmDevice = "/dev/dri/card0";
    const char* socketName = "imway-0";
    const char* screenshotPath = nullptr;
    const char* controlPath = nullptr;
    int outW = 1280, outH = 800;
    double hz = 60.0;
    int framesLimit = 0;

    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage(argv[0]);
                exit(2);
            }

            return argv[++i];
        };

        if (!strcmp(argv[i], "--socket")) {
            socketName = next();
        } else if (!strcmp(argv[i], "--size")) {
            if (!parseSize(next(), outW, outH)) {
                usage(argv[0]);

                return 2;
            }
        } else if (!strcmp(argv[i], "--hz")) {
            hz = atof(next());
        } else if (!strcmp(argv[i], "--frames")) {
            framesLimit = atoi(next());
        } else if (!strcmp(argv[i], "--screenshot")) {
            screenshotPath = next();
        } else if (!strcmp(argv[i], "--control")) {
            controlPath = next();
        } else if (!strcmp(argv[i], "--backend")) {
            kms = !strcmp(next(), "kms");
        } else if (!strcmp(argv[i], "--drm-device")) {
            drmDevice = next();
        } else {
            usage(argv[0]);

            return 2;
        }
    }

    if (!getenv("XDG_RUNTIME_DIR")) {
        sysE << "XDG_RUNTIME_DIR is not set"_sv << endL;

        return 1;
    }

    ObjPool::Ref pool = ObjPool::fromMemory();
    struct ev_loop* loop = ev_default_loop(0);

    try {
        auto* scene = pool->make<Scene>();

        scene->outW = outW;
        scene->outH = outH;
        scene->hz = hz;

        ::Output* output = kms ? ::Output::createKms(pool.mutPtr(), loop, drmDevice) : ::Output::createHeadless(pool.mutPtr(), outW, outH, hz);

        if (kms) {
            scene->outW = output->width();
            scene->outH = output->height();
            scene->hz = output->refresh();
            scene->drawCursor = true;
        }

        STD_VERIFY(output->start());

        Renderer* renderer = Renderer::create(pool.mutPtr(), loop, *scene, *output, framesLimit);

        Vector<DmabufFormat> formats;

        for (size_t i = 0; i < renderer->dmabufFormatCount(); i++) {
            formats.pushBack(renderer->dmabufFormat(i));
        }

        WaylandConfig wcfg;

        wcfg.socketName = socketName;
        wcfg.formats = formats.data();
        wcfg.formatCount = formats.length();

        Wayland* wayland = Wayland::create(pool.mutPtr(), loop, *scene, wcfg);

        renderer->setFrameListener(wayland->frameListener());

        InputSink* sink = InputSink::tee(pool.mutPtr(), *renderer->sink(), *wayland->sink());

        if (kms) {
            try {
                InputSource::createLibinput(pool.mutPtr(), loop, *sink, scene->outW, scene->outH);
            } catch (...) {
                sysE << "imway: no input, mouse is dead: "_sv << Exception::current() << endL;
            }
        }

        if (controlPath) {
            Control::create(pool.mutPtr(), loop, *sink, *renderer, controlPath);
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
