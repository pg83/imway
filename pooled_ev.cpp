#include "pooled_ev.h"

#include <std/mem/obj_pool.h>

#include <ev.h>

using namespace stl;

namespace {
    struct IoBox {
        struct ev_loop* loop = nullptr;
        ev_io value{};

        IoBox(struct ev_loop* l);
        ~IoBox() noexcept;
    };

    struct TimerBox {
        struct ev_loop* loop = nullptr;
        ev_timer value{};

        TimerBox(struct ev_loop* l);
        ~TimerBox() noexcept;
    };

    struct PrepareBox {
        struct ev_loop* loop = nullptr;
        ev_prepare value{};

        PrepareBox(struct ev_loop* l);
        ~PrepareBox() noexcept;
    };

    struct SignalBox {
        struct ev_loop* loop = nullptr;
        ev_signal value{};

        SignalBox(struct ev_loop* l);
        ~SignalBox() noexcept;
    };
}

IoBox::IoBox(struct ev_loop* l)
    : loop(l)
{
}

IoBox::~IoBox() noexcept {
    if (ev_is_active(&value)) {
        ev_io_stop(loop, &value);
    }
}

ev_io* createEvIo(ObjPool& pool, struct ev_loop* loop) {
    return &pool.make<IoBox>(loop)->value;
}

TimerBox::TimerBox(struct ev_loop* l)
    : loop(l)
{
}

TimerBox::~TimerBox() noexcept {
    if (ev_is_active(&value)) {
        ev_timer_stop(loop, &value);
    }
}

ev_timer* createEvTimer(ObjPool& pool, struct ev_loop* loop) {
    return &pool.make<TimerBox>(loop)->value;
}

PrepareBox::PrepareBox(struct ev_loop* l)
    : loop(l)
{
}

PrepareBox::~PrepareBox() noexcept {
    if (ev_is_active(&value)) {
        ev_prepare_stop(loop, &value);
    }
}

ev_prepare* createEvPrepare(ObjPool& pool, struct ev_loop* loop) {
    return &pool.make<PrepareBox>(loop)->value;
}

SignalBox::SignalBox(struct ev_loop* l)
    : loop(l)
{
}

SignalBox::~SignalBox() noexcept {
    if (ev_is_active(&value)) {
        ev_signal_stop(loop, &value);
    }
}

ev_signal* createEvSignal(ObjPool& pool, struct ev_loop* loop) {
    return &pool.make<SignalBox>(loop)->value;
}
