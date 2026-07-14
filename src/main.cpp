#include "server.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    void usage(const char* argv0) {
        sysE << "usage: "_sv << argv0
             << " [--backend headless|kms] [--drm-device PATH] [--socket NAME]"
                " [--size WxH] [--hz N] [--frames N] [--screenshot PATH] [--control FIFO]"_sv
             << endL;
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
    ServerConfig cfg;

    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage(argv[0]);
                exit(2);
            }

            return argv[++i];
        };

        if (!strcmp(argv[i], "--socket")) {
            cfg.socketName = next();
        } else if (!strcmp(argv[i], "--size")) {
            if (!parseSize(next(), cfg.outW, cfg.outH)) {
                usage(argv[0]);

                return 2;
            }
        } else if (!strcmp(argv[i], "--hz")) {
            cfg.hz = atof(next());
        } else if (!strcmp(argv[i], "--frames")) {
            cfg.framesLimit = atoi(next());
        } else if (!strcmp(argv[i], "--screenshot")) {
            cfg.screenshotPath = next();
        } else if (!strcmp(argv[i], "--control")) {
            cfg.controlPath = next();
        } else if (!strcmp(argv[i], "--backend")) {
            cfg.backend = next();
        } else if (!strcmp(argv[i], "--drm-device")) {
            cfg.drmDevice = next();
        } else {
            usage(argv[0]);

            return 2;
        }
    }

    if (!getenv("XDG_RUNTIME_DIR")) {
        sysE << "XDG_RUNTIME_DIR is not set"_sv << endL;

        return 1;
    }

    // пул — владелец всего графа объектов сервера; умирает при выходе из main
    ObjPool::Ref pool = ObjPool::fromMemory();

    try {
        Server* server = Server::create(pool.mutPtr(), cfg);

        server->run();
        sysO << "imway: clean exit after "_sv << server->framesDone << " frames"_sv << endL;
    } catch (...) {
        sysE << "imway: fatal: "_sv << Exception::current() << endL;

        return 1;
    }

    return 0;
}
