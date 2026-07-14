#include "control.h"
#include "device.h"
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
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    void usage(const char* argv0) {
        sysE << "usage: "_sv << argv0 << " [--device headless|auto|/dev/dri/cardN] [--output NAME] [--mode WxH@HZ]" " [--socket NAME] [--frames N] [--screenshot PATH] [--control FIFO] [--list]"_sv << endL;
    }
}

int main(int argc, char** argv) {
    const char* devicePath = "headless";
    const char* outputName = nullptr;
    const char* modeStr = nullptr;
    const char* socketName = "imway-0";
    const char* screenshotPath = nullptr;
    const char* controlPath = nullptr;
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
        } else if (!strcmp(argv[i], "--device")) {
            devicePath = next();
        } else if (!strcmp(argv[i], "--output")) {
            outputName = next();
        } else if (!strcmp(argv[i], "--mode")) {
            modeStr = next();
        } else if (!strcmp(argv[i], "--frames")) {
            framesLimit = atoi(next());
        } else if (!strcmp(argv[i], "--screenshot")) {
            screenshotPath = next();
        } else if (!strcmp(argv[i], "--control")) {
            controlPath = next();
        } else if (!strcmp(argv[i], "--list")) {
            Device::list();

            return 0;
        } else {
            usage(argv[0]);

            return 2;
        }
    }

    bool kms = strcmp(devicePath, "headless") != 0;

    if (!getenv("XDG_RUNTIME_DIR")) {
        sysE << "XDG_RUNTIME_DIR is not set"_sv << endL;

        return 1;
    }

    ObjPool::Ref pool = ObjPool::fromMemory();
    struct ev_loop* loop = ev_default_loop(0);

    try {
        auto* scene = pool->make<Scene>();

        Device* device = kms ? Device::createKms(pool.mutPtr(), loop, strcmp(devicePath, "auto") ? devicePath : nullptr) : Device::createHeadless(pool.mutPtr(), loop);

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
        wcfg.formats = formats.data();
        wcfg.formatCount = formats.length();

        Wayland* wayland = Wayland::create(pool.mutPtr(), loop, *scene, wcfg);

        Renderer* renderer = device->createRenderer(*scene, *output, *wayland->frameListener(), framesLimit);

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
