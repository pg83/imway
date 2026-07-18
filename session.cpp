#include "session.h"

#include "composer.h"
#include "intr_list.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <ev.h>

extern "C" {
#include <libseat.h>
}

#include <std/dbg/verify.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

using namespace stl;

namespace {
    struct DirectSession: public Session {
        StringView seatName() const override;
        int openDevice(const char* path) override;
        void closeDevice(int fd) override;
    };

    void seatEnableCb(libseat*, void* data);
    void seatDisableCb(libseat*, void* data);
    void seatIoCb(struct ev_loop*, ev_io* w, int);

    struct SeatDevice {
        int id = -1;
        int fd = -1;
    };

    struct SeatSession: public Session {
        Composer* c = nullptr;
        struct ev_loop* loop = nullptr;
        libseat* seat = nullptr;
        bool active = false;
        ev_io io{};
        Vector<SeatDevice> devices;

        SeatSession(Composer& comp);
        ~SeatSession() noexcept;

        StringView seatName() const override;

        int openDevice(const char* path) override;
        void closeDevice(int fd) override;

        void enable();
        void disable();
    };

    void seatEnableCb(libseat*, void* data) {
        ((SeatSession*)data)->enable();
    }

    void seatDisableCb(libseat*, void* data) {
        ((SeatSession*)data)->disable();
    }

    void seatIoCb(struct ev_loop* loop, ev_io* w, int) {
        // seatd death leaves the fd forever readable: without this check the
        // level-triggered watcher busy-spins at 100% cpu making no progress
        if (libseat_dispatch(((SeatSession*)w->data)->seat, 0) < 0 && errno != EAGAIN) {
            sysE << "imway: seat connection lost, exiting"_sv << endL;
            ev_io_stop(loop, w);
            ev_break(loop, EVBREAK_ALL);
        }
    }

    const libseat_seat_listener kSeatListener = {
        .enable_seat = seatEnableCb,
        .disable_seat = seatDisableCb,
    };
}

StringView DirectSession::seatName() const {
    return "seat0"_sv;
}

int DirectSession::openDevice(const char* path) {
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);

    return fd < 0 ? -errno : fd;
}

void DirectSession::closeDevice(int fd) {
    close(fd);
}

SeatSession::SeatSession(Composer& comp)
    : c(&comp)
    , loop(comp.loop)
{
    seat = libseat_open_seat(&kSeatListener, this);

    if (!seat) {
        Errno().raise(StringBuilder() << "libseat: no seat available (seatd/logind)"_sv);
    }

    for (int i = 0; i < 100 && !active; i++) {
        if (libseat_dispatch(seat, 100) < 0) {
            break;
        }
    }

    if (!active) {
        libseat_close_seat(seat);
        seat = nullptr;
        Errno().raise(StringBuilder() << "libseat: seat did not become active"_sv);
    }

    ev_io_init(&io, seatIoCb, libseat_get_fd(seat), EV_READ);
    io.data = this;
    ev_io_start(loop, &io);
}

SeatSession::~SeatSession() noexcept {
    if (!seat) {
        return;
    }

    ev_io_stop(loop, &io);
    libseat_close_seat(seat);
    seat = nullptr;
}

StringView SeatSession::seatName() const {
    return StringView(libseat_seat_name(seat));
}

int SeatSession::openDevice(const char* path) {
    int fd = -1;
    int id = libseat_open_device(seat, path, &fd);

    if (id < 0) {
        return -errno;
    }

    devices.pushBack({id, fd});

    return fd;
}

void SeatSession::closeDevice(int fd) {
    for (size_t i = 0; i < devices.length(); i++) {
        if (devices[i].fd == fd) {
            libseat_close_device(seat, devices[i].id);
            devices.mut(i) = devices.back();
            devices.popBack();

            break;
        }
    }

    close(fd);
}

void SeatSession::enable() {
    active = true;

    forEach<SessionListener>(c->sessionListeners, [](SessionListener& listener) {
        listener.sessionEnabled();
    });
}

void SeatSession::disable() {
    active = false;

    forEach<SessionListener>(c->sessionListeners, [](SessionListener& listener) {
        listener.sessionDisabled();
    });

    libseat_disable_seat(seat);
}

Session* Session::create(Composer& c) {
    return c.pool->make<SeatSession>(c);
}

Session* Session::createDirect(Composer& c) {
    return c.pool->make<DirectSession>();
}
