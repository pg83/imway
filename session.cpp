#include "session.h"

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
        const char* seatName() const override {
            return "seat0";
        }

        int openDevice(const char* path) override {
            int fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);

            return fd < 0 ? -errno : fd;
        }

        void closeDevice(int fd) override {
            close(fd);
        }

        void addListener(SessionListener*) override {
        }
    };

    void seatEnableCb(libseat*, void* data);
    void seatDisableCb(libseat*, void* data);
    void seatIoCb(struct ev_loop*, ev_io* w, int);

    struct SeatDevice {
        int id = -1;
        int fd = -1;
    };

    struct SeatSession: public Session {
        struct ev_loop* loop = nullptr;
        libseat* seat = nullptr;
        bool active = false;
        ev_io io{};
        Vector<SessionListener*> listeners;
        Vector<SeatDevice> devices;

        SeatSession(struct ev_loop* evLoop);
        ~SeatSession() noexcept;

        const char* seatName() const override {
            return libseat_seat_name(seat);
        }

        int openDevice(const char* path) override;
        void closeDevice(int fd) override;

        void addListener(SessionListener* l) override {
            listeners.pushBack(l);
        }

        void enable();
        void disable();
    };

    void seatEnableCb(libseat*, void* data) {
        ((SeatSession*)data)->enable();
    }

    void seatDisableCb(libseat*, void* data) {
        ((SeatSession*)data)->disable();
    }

    void seatIoCb(struct ev_loop*, ev_io* w, int) {
        libseat_dispatch(((SeatSession*)w->data)->seat, 0);
    }

    const libseat_seat_listener kSeatListener = {
        .enable_seat = seatEnableCb,
        .disable_seat = seatDisableCb,
    };
}

SeatSession::SeatSession(struct ev_loop* evLoop)
    : loop(evLoop)
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

    for (SessionListener* l : listeners) {
        l->sessionEnabled();
    }
}

void SeatSession::disable() {
    active = false;

    for (SessionListener* l : listeners) {
        l->sessionDisabled();
    }

    libseat_disable_seat(seat);
}

Session* Session::create(ObjPool* pool, struct ev_loop* loop) {
    return pool->make<SeatSession>(loop);
}

Session* Session::createDirect(ObjPool* pool) {
    return pool->make<DirectSession>();
}
