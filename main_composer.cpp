#include "main_composer.h"

#include "crash_trace.h"

#include "composer.h"
#include "control.h"
#include "dbus_conn.h"
#include "device.h"
#include "device_headless.h"
#include "device_kms.h"
#include "icon_pool.h"
#include "icon_provider.h"
#include "icon_store.h"
#include "input.h"
#include "keyboard.h"
#include "log.h"
#include "log_extern.h"
#include "main_supervisor.h"
#include "mixer.h"
#include "notifications.h"
#include "notifier.h"
#include "output.h"
#include "renderer.h"
#include "scene.h"
#include "status_notifier.h"
#include "session.h"
#include "small_obj_allocator.h"
#include "util.h"
#include "wayland.h"
#include "wifi.h"

#include <stdlib.h>
#include <unistd.h>

#include <ev.h>

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>
#include <std/thr/pool.h>

using namespace stl;

namespace {
    struct Config {
        StringView devicePath = "auto";
        StringView outputName;
        StringView mode;
        StringView socketName = "imway-0";
        StringView xkbLayout = "us,ru";
        StringView xkbOptions = "grp:caps_toggle";
        StringView fontPath;
        float uiScale = 1.f;
        StringView screenshotPath;
        StringView controlPath;
        int framesLimit = 0;
        double dpmsSec = 0;
        OutputConfiguration outputColor;
        char** cmdArgv = nullptr;
    };

    void usage(Log& log, const char* argv0) {
        log << "usage: "_sv << argv0 << " [--device auto|headless|/dev/dri/cardN] [--output NAME] [--mode WxH@HZ]" " [--socket NAME] [--xkb-layout L] [--xkb-options O] [--font PATH] [--scale K]" " [--frames N] [--screenshot PATH] [--control FIFO] [--dpms SEC] [--hdr SDR_WHITE_NITS]" " [--hdr-min NITS] [--hdr-peak NITS] [--hdr-fall NITS] [--bpc BITS]" " [--rgb-range auto|full|limited] [--list] [-- CMD ARG...]"_sv << endL;
    }

}

int mainComposer(int argc, char** argv) {
    installCrashTracer();

    // stdin/stdout initially name the same full-duplex supervisor socket.
    // Protocol traffic stays on stdin; ordinary output belongs in the IX log.
    if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
        return 1;
    }

    ObjPool::Ref pool = ObjPool::fromMemory();
    // the log is the pool's very first object: it outlives everything else,
    // and every line from here on goes through it (teeing to the IX log)
    Log* log = Log::create(pool.mutPtr(), &stderrStream());

    Config cfg;

    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> StringView {
            if (i + 1 >= argc) {
                usage(*log, argv[0]);
                exit(2);
            }

            return argv[++i];
        };

        StringView arg(argv[i]);

        if (arg == "--socket"_sv) {
            cfg.socketName = next();
        } else if (arg == "--device"_sv) {
            cfg.devicePath = next();
        } else if (arg == "--output"_sv) {
            cfg.outputName = next();
        } else if (arg == "--mode"_sv) {
            cfg.mode = next();
        } else if (arg == "--xkb-layout"_sv) {
            cfg.xkbLayout = next();
        } else if (arg == "--xkb-options"_sv) {
            cfg.xkbOptions = next();
        } else if (arg == "--font"_sv) {
            cfg.fontPath = next();
        } else if (arg == "--scale"_sv) {
            cfg.uiScale = (float)parseFloat(StringView(next()));

            if (!(cfg.uiScale > 0.f)) {
                usage(*log, argv[0]);

                return 2;
            }
        } else if (arg == "--frames"_sv) {
            cfg.framesLimit = (int)StringView(next()).stou();
        } else if (arg == "--dpms"_sv) {
            cfg.dpmsSec = parseFloat(StringView(next()));
        } else if (arg == "--hdr"_sv) {
            cfg.outputColor.hdrSdrWhiteNits = parseFloat(StringView(next()));
        } else if (arg == "--hdr-min"_sv) {
            cfg.outputColor.displayMinNits = parseFloat(StringView(next()));
        } else if (arg == "--hdr-peak"_sv) {
            cfg.outputColor.displayPeakNits = parseFloat(StringView(next()));
        } else if (arg == "--hdr-fall"_sv) {
            cfg.outputColor.displayMaxFallNits = parseFloat(StringView(next()));
        } else if (arg == "--bpc"_sv) {
            cfg.outputColor.bpc = (u32)StringView(next()).stou();
        } else if (arg == "--rgb-range"_sv) {
            StringView range = next();

            if (range == "auto"_sv) {
                cfg.outputColor.range = OutputRange::automatic;
            } else if (range == "full"_sv) {
                cfg.outputColor.range = OutputRange::full;
            } else if (range == "limited"_sv) {
                cfg.outputColor.range = OutputRange::limited;
            } else {
                usage(*log, argv[0]);
                return 2;
            }
        } else if (arg == "--screenshot"_sv) {
            cfg.screenshotPath = next();
        } else if (arg == "--control"_sv) {
            cfg.controlPath = next();
        } else if (arg == "--"_sv) {
            if (i + 1 >= argc) {
                usage(*log, argv[0]);

                return 2;
            }

            cfg.cmdArgv = argv + i + 1;

            break;
        } else if (arg == "--list"_sv) {
            DeviceKms::list();

            return 0;
        } else {
            usage(*log, argv[0]);

            return 2;
        }
    }

    const OutputConfiguration& oc = cfg.outputColor;

    if (oc.hdrSdrWhiteNits < 0 || oc.displayMinNits < 0 ||
        oc.displayPeakNits < 0 || oc.displayMaxFallNits < 0 ||
        (oc.bpc && oc.bpc != 8 && oc.bpc != 10 && oc.bpc != 12) ||
        (!oc.hdrSdrWhiteNits &&
         (oc.displayMinNits || oc.displayPeakNits || oc.displayMaxFallNits)) ||
        (oc.displayMinNits && oc.displayPeakNits &&
         oc.displayMinNits >= oc.displayPeakNits) ||
        (oc.displayMaxFallNits && oc.displayPeakNits &&
         oc.displayMaxFallNits > oc.displayPeakNits)) {
        usage(*log, argv[0]);
        return 2;
    }

    bool kms = cfg.devicePath != "headless"_sv;

    if (!getenv("XDG_RUNTIME_DIR")) {
        *log << "XDG_RUNTIME_DIR is not set"_sv << endL;

        return 1;
    }

    // the wiring board is the pool's second object, right after the log:
    // LIFO death makes it outlive every subsystem holding the reference
    Composer& c = *pool->make<Composer>(pool.mutPtr());
    struct ev_loop* loop = ev_default_loop(0);

    c.log = log;
    installExternLogHandlers(c);
    c.alloc = SmallObjAllocator::create(pool.mutPtr());
    c.loop = loop;
    c.offload = ThreadPool::simple(c.pool, 1);
    c.supervisor = Supervisor::create(c);

    try {
        auto* scene = pool->make<Scene>();

        c.scene = scene;

        Session* session = nullptr;

        if (kms) {
            try {
                session = Session::create(c);
                *log << "imway: libseat session on "_sv << session->seatName() << endL;
            } catch (...) {
                *log << "imway: "_sv << Exception::current() << ", opening devices directly"_sv << endL;
                session = Session::createDirect(c);
            }
        }

        c.session = session;

        Device* device = kms ? DeviceKms::create(c, cfg.devicePath == "auto"_sv ? StringView{} : cfg.devicePath) : DeviceHeadless::create(c);

        c.device = device;

        ::Output* output = device->createOutput(cfg.outputName, cfg.mode, cfg.outputColor);

        c.output = output;

        scene->outW = output->width();
        scene->outH = output->height();
        scene->workW = scene->outW;
        scene->workH = scene->outH;
        scene->hz = output->refresh();
        scene->drawCursor = kms;

#ifdef IMWAY_FOR_TESTS
        // headless scenarios assert on the software cursor in screenshots
        scene->drawCursor = scene->drawCursor || getenv("IMWAY_FORCE_CURSOR");
#endif
        scene->socketName = cfg.socketName;

        STD_VERIFY(output->start());

        Vector<DmabufFormat> formats;

        device->dmabufFormats([&formats](const DmabufFormat& f) {
            formats.pushBack(f);
        });

        Vector<DmabufFormat> scanoutFormats;

        output->scanoutFormats([&scanoutFormats](const DmabufFormat& f) {
            scanoutFormats.pushBack(f);
        });

        Keyboard* kb = Keyboard::create(pool.mutPtr(), *log, cfg.xkbLayout, cfg.xkbOptions);

        c.kb = kb;

        WaylandConfig wcfg;

        wcfg.socketName = cfg.socketName;
        wcfg.formats = formats.data();
        wcfg.formatCount = formats.length();
        wcfg.scanoutFormats = scanoutFormats.data();
        wcfg.scanoutFormatCount = scanoutFormats.length();
        wcfg.mainDevice = device->renderDevice();
        wcfg.output = output;
        wcfg.dpmsSec = kms ? cfg.dpmsSec : 0;
        wcfg.drmFd = device->drmFd();
        wcfg.explicitSync = device->explicitSyncSupported();

        c.iconPool = IconPool::create(pool.mutPtr());
        c.iconProviders.pushBack(IconStore::create(c));

        // the notifier store always exists (internal senders like wifi
        // post to it regardless of a bus); the dbus service is layered on
        // top only when the session bus is reachable
        c.notifier = Notifier::create(c);
        c.bus = DBusConn::create(pool.mutPtr(), c.alloc, loop, *log, false);

        if (c.bus) {
            c.notes = Notifications::create(c);
            c.statusNotifier = StatusNotifier::create(c);
        }

        c.mixer = Mixer::create(c);

        c.sysbus = DBusConn::create(pool.mutPtr(), c.alloc, loop, *log, true);
        c.wifi = c.sysbus ? Wifi::create(c) : nullptr;

        Wayland* wayland = Wayland::create(c, wcfg);

        c.wayland = wayland;

        if (c.bus) {
            // the socket is bound by now: dbus activation learns the real
            // name instead of a wrapper-script guess
            c.bus->setActivationEnv("WAYLAND_DISPLAY"_sv, cfg.socketName);
            c.bus->setActivationEnv("XDG_CURRENT_DESKTOP"_sv, "imway"_sv);
        }

        Renderer* renderer = device->createRenderer(c, cfg.fontPath, cfg.uiScale, cfg.framesLimit);

        c.renderer = renderer;

        if (kms) {
            try {
                InputSource::createLibinput(c);
            } catch (...) {
                *log << "imway: no input, mouse is dead: "_sv << Exception::current() << endL;
            }
        }

        if (!cfg.controlPath.empty()) {
#ifdef IMWAY_FOR_TESTS
            Control::create(c, cfg.controlPath);
#else
            *log << "imway: --control is a test-build feature, ignored"_sv << endL;
#endif
        }

        if (cfg.cmdArgv) {
            Vector<StringView> args;

            for (char** arg = cfg.cmdArgv; *arg; arg++) {
                args.pushBack(StringView(*arg));
            }

            StringBuilder display;

            display << "WAYLAND_DISPLAY="_sv << cfg.socketName;

            StringView env[] = {sv(display)};
            SupervisorSpawn spawn;

            spawn.args = args.data();
            spawn.argCount = args.length();
            spawn.env = env;
            spawn.envCount = 1;

            c.supervisor->spawn(spawn);
        }

        wayland->run();

        if (!cfg.screenshotPath.empty()) {
            renderer->screenshot(cfg.screenshotPath);
            *log << "imway: screenshot: "_sv << cfg.screenshotPath << endL;
        }

        *log << "imway: clean exit after "_sv << scene->framesDone << " frames"_sv << endL;
    } catch (...) {
        *log << "imway: fatal: "_sv << Exception::current() << endL;

        return 1;
    }

    return 0;
}
