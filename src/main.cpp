#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "server.hpp"

static void usage(const char* argv0) {
    std::printf("usage: %s [--backend headless|kms] [--drm-device PATH] [--socket NAME] "
                "[--size WxH] [--hz N] [--frames N] [--screenshot PATH] [--control FIFO]\n",
                argv0);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0); // логи видны сразу и при редиректе в файл
    Server server;

    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage(argv[0]);
                exit(2);
            }
            return argv[++i];
        };
        if (!strcmp(argv[i], "--socket")) {
            server.socket_name = next();
        } else if (!strcmp(argv[i], "--size")) {
            if (sscanf(next(), "%dx%d", &server.out_w, &server.out_h) != 2) {
                usage(argv[0]);
                return 2;
            }
        } else if (!strcmp(argv[i], "--hz")) {
            server.hz = atof(next());
        } else if (!strcmp(argv[i], "--frames")) {
            server.frames_limit = atoi(next());
        } else if (!strcmp(argv[i], "--screenshot")) {
            server.screenshot_path = next();
        } else if (!strcmp(argv[i], "--control")) {
            server.control_path = next();
        } else if (!strcmp(argv[i], "--backend")) {
            server.backend = next();
        } else if (!strcmp(argv[i], "--drm-device")) {
            server.drm_device = next();
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!getenv("XDG_RUNTIME_DIR")) {
        std::fprintf(stderr, "XDG_RUNTIME_DIR не установлен\n");
        return 1;
    }

    if (!server.init()) return 1;
    server.run();
    server.finish();
    std::printf("imway: чистый выход после %d кадров\n", server.frames_done);
    return 0;
}
