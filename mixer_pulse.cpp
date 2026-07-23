#include "composer.h"
#include "log.h"
#include "intr_list.h"
#include "listener.h"
#include "mixer.h"
#include "mixer_pulse.h"
#include "pooled.h"
#include "small_obj_allocator.h"

#if __has_include(<pulse/pulseaudio.h>)

    #include "scene.h"
    #include "util.h"

    #include <time.h>

    #include <ev.h>

    #include <pulse/pulseaudio.h>

    #include <std/ios/sys.h>
    #include <std/mem/obj_pool.h>

using namespace stl;

// --- libpulse mainloop glued onto libev ----------------------------------
// pulse ships no loop of its own; a pa_mainloop_api is a set of hooks the
// same way libdbus hands out watches. io events ride ev_io, time events
// ev_timer, defer events ev_prepare — the exact shape of dbus_conn.cpp

struct pa_io_event {
    ev_io io{};
    struct ev_loop* loop = nullptr;
    pa_mainloop_api* api = nullptr;
    pa_io_event_cb_t cb = nullptr;
    pa_io_event_destroy_cb_t destroy = nullptr;
    void* userdata = nullptr;
};

struct pa_time_event {
    ev_timer timer{};
    struct ev_loop* loop = nullptr;
    pa_mainloop_api* api = nullptr;
    pa_time_event_cb_t cb = nullptr;
    pa_time_event_destroy_cb_t destroy = nullptr;
    void* userdata = nullptr;
};

struct pa_defer_event {
    ev_prepare prep{};
    struct ev_loop* loop = nullptr;
    pa_mainloop_api* api = nullptr;
    pa_defer_event_cb_t cb = nullptr;
    pa_defer_event_destroy_cb_t destroy = nullptr;
    void* userdata = nullptr;
    bool started = false;
};

namespace {
    // the allocator context behind pa_mainloop_api::userdata: event objects
    // come from the composer's small-object allocator (the dbus_conn WatchBox
    // pattern), not the raw heap. Kept trivially destructible: the pool
    // preserves the storage until its own death, so PulseMixer's pooledGuard
    // teardown — which runs after the impl dies and frees the surviving
    // events through io_free/time_free/defer_free — still releases safely
    struct PulseApi {
        pa_mainloop_api api{};
        struct ev_loop* loop = nullptr;
        SmallObjAllocator* alloc = nullptr;

        PulseApi(SmallObjAllocator* a, struct ev_loop* l);
    };

    PulseApi* apiCtx(pa_mainloop_api* a) {
        return (PulseApi*)a->userdata;
    }

    int toEv(pa_io_event_flags_t f) {
        int e = 0;

        if (f & PA_IO_EVENT_INPUT) {
            e |= EV_READ;
        }

        if (f & PA_IO_EVENT_OUTPUT) {
            e |= EV_WRITE;
        }

        return e;
    }

    void ioEvCb(struct ev_loop*, ev_io* w, int revents) {
        auto* e = (pa_io_event*)w->data;
        pa_io_event_flags_t f = PA_IO_EVENT_NULL;

        if (revents & EV_READ) {
            f = (pa_io_event_flags_t)(f | PA_IO_EVENT_INPUT);
        }

        if (revents & EV_WRITE) {
            f = (pa_io_event_flags_t)(f | PA_IO_EVENT_OUTPUT);
        }

        e->cb(e->api, e, w->fd, f, e->userdata);
    }

    pa_io_event* ioNew(pa_mainloop_api* a, int fd, pa_io_event_flags_t f, pa_io_event_cb_t cb, void* userdata) {
        pa_io_event* e = apiCtx(a)->alloc->make<pa_io_event>();

        e->loop = apiCtx(a)->loop;
        e->api = a;
        e->cb = cb;
        e->userdata = userdata;
        ev_io_init(&e->io, ioEvCb, fd, toEv(f));
        e->io.data = e;
        ev_io_start(e->loop, &e->io);

        return e;
    }

    void ioEnable(pa_io_event* e, pa_io_event_flags_t f) {
        ev_io_stop(e->loop, &e->io);
        ev_io_set(&e->io, e->io.fd, toEv(f));
        ev_io_start(e->loop, &e->io);
    }

    void ioFree(pa_io_event* e) {
        ev_io_stop(e->loop, &e->io);

        if (e->destroy) {
            e->destroy(e->api, e, e->userdata);
        }

        apiCtx(e->api)->alloc->release(e);
    }

    void ioSetDestroy(pa_io_event* e, pa_io_event_destroy_cb_t cb) {
        e->destroy = cb;
    }

    double delayUntil(const struct timeval* tv) {
        if (!tv) {
            return 0.;
        }

        struct timespec now{};

        clock_gettime(CLOCK_MONOTONIC, &now);

        double target = (double)tv->tv_sec + (double)tv->tv_usec / 1e6;
        double n = (double)now.tv_sec + (double)now.tv_nsec / 1e9;
        double d = target - n;

        return d < 0. ? 0. : d;
    }

    void timeEvCb(struct ev_loop*, ev_timer* w, int) {
        auto* e = (pa_time_event*)w->data;
        struct timeval tv{};

        e->cb(e->api, e, &tv, e->userdata);
    }

    pa_time_event* timeNew(pa_mainloop_api* a, const struct timeval* tv, pa_time_event_cb_t cb, void* userdata) {
        pa_time_event* e = apiCtx(a)->alloc->make<pa_time_event>();

        e->loop = apiCtx(a)->loop;
        e->api = a;
        e->cb = cb;
        e->userdata = userdata;
        ev_timer_init(&e->timer, timeEvCb, delayUntil(tv), 0.);
        e->timer.data = e;
        ev_timer_start(e->loop, &e->timer);

        return e;
    }

    void timeRestart(pa_time_event* e, const struct timeval* tv) {
        ev_timer_stop(e->loop, &e->timer);
        ev_timer_set(&e->timer, delayUntil(tv), 0.);
        ev_timer_start(e->loop, &e->timer);
    }

    void timeFree(pa_time_event* e) {
        ev_timer_stop(e->loop, &e->timer);

        if (e->destroy) {
            e->destroy(e->api, e, e->userdata);
        }

        apiCtx(e->api)->alloc->release(e);
    }

    void timeSetDestroy(pa_time_event* e, pa_time_event_destroy_cb_t cb) {
        e->destroy = cb;
    }

    void deferPrepCb(struct ev_loop*, ev_prepare* w, int) {
        auto* e = (pa_defer_event*)w->data;

        e->cb(e->api, e, e->userdata);
    }

    pa_defer_event* deferNew(pa_mainloop_api* a, pa_defer_event_cb_t cb, void* userdata) {
        pa_defer_event* e = apiCtx(a)->alloc->make<pa_defer_event>();

        e->loop = apiCtx(a)->loop;
        e->api = a;
        e->cb = cb;
        e->userdata = userdata;
        ev_prepare_init(&e->prep, deferPrepCb);
        e->prep.data = e;
        ev_prepare_start(e->loop, &e->prep);
        e->started = true;

        return e;
    }

    void deferEnable(pa_defer_event* e, int b) {
        if (b && !e->started) {
            ev_prepare_start(e->loop, &e->prep);
            e->started = true;
        } else if (!b && e->started) {
            ev_prepare_stop(e->loop, &e->prep);
            e->started = false;
        }
    }

    void deferFree(pa_defer_event* e) {
        if (e->started) {
            ev_prepare_stop(e->loop, &e->prep);
        }

        if (e->destroy) {
            e->destroy(e->api, e, e->userdata);
        }

        apiCtx(e->api)->alloc->release(e);
    }

    void deferSetDestroy(pa_defer_event* e, pa_defer_event_destroy_cb_t cb) {
        e->destroy = cb;
    }

    void apiQuit(pa_mainloop_api*, int) {
    }

    void fillApi(PulseApi& ctx) {
        pa_mainloop_api& api = ctx.api;

        api.userdata = &ctx;
        api.io_new = ioNew;
        api.io_enable = ioEnable;
        api.io_free = ioFree;
        api.io_set_destroy = ioSetDestroy;
        api.time_new = timeNew;
        api.time_restart = timeRestart;
        api.time_free = timeFree;
        api.time_set_destroy = timeSetDestroy;
        api.defer_new = deferNew;
        api.defer_enable = deferEnable;
        api.defer_free = deferFree;
        api.defer_set_destroy = deferSetDestroy;
        api.quit = apiQuit;
    }
}

PulseApi::PulseApi(SmallObjAllocator* a, struct ev_loop* l)
    : loop(l)
    , alloc(a)
{
}

// --- the mixer ------------------------------------------------------------

namespace {
    struct PulseMixer;

    void stateCb(pa_context* ctx, void* data);
    void subscribeCb(pa_context* ctx, pa_subscription_event_type_t t, u32 idx, void* data);
    void serverInfoCb(pa_context* ctx, const pa_server_info* info, void* data);
    void sinkInfoCb(pa_context* ctx, const pa_sink_info* info, int eol, void* data);

    struct PulseMixer: public Mixer {
        Composer* c = nullptr;
        PulseApi* papi = nullptr;
        pa_context* ctx = nullptr;

        bool ready = false;
        u32 sinkIndex = PA_INVALID_INDEX;
        u8 channels = 2;
        float vol = 0.f;
        bool mute = false;

        PulseMixer(Composer& comp);

        float volume() override;
        void setVolume(float v) override;
        bool muted() override;
        void setMuted(bool m) override;

        void queryDefaultSink();
        void updateSink(const pa_sink_info* info);
        void notify();
    };
}

PulseMixer::PulseMixer(Composer& comp)
    : c(&comp)
{
    papi = comp.pool->make<PulseApi>(comp.alloc, comp.loop);
    fillApi(*papi);
    ctx = pa_context_new(&papi->api, "imway");

    pa_context* held = ctx;

    pooledGuard(*comp.pool, [held] {
        // the impl is gone by now: silence the state callback before the
        // disconnect would fire it
        pa_context_set_state_callback(held, nullptr, nullptr);
        pa_context_disconnect(held);
        pa_context_unref(held);
    });
    pa_context_set_state_callback(ctx, stateCb, this);
    pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
}

float PulseMixer::volume() {
    return vol;
}

void PulseMixer::setVolume(float v) {
    v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
    vol = v;

    if (!ready || sinkIndex == PA_INVALID_INDEX) {
        return;
    }

    pa_cvolume cv;

    pa_cvolume_set(&cv, channels, (pa_volume_t)(v * (float)PA_VOLUME_NORM));
    pa_operation_unref(pa_context_set_sink_volume_by_index(ctx, sinkIndex, &cv, nullptr, nullptr));
    notify();
}

bool PulseMixer::muted() {
    return mute;
}

void PulseMixer::setMuted(bool m) {
    mute = m;

    if (!ready || sinkIndex == PA_INVALID_INDEX) {
        return;
    }

    pa_operation_unref(pa_context_set_sink_mute_by_index(ctx, sinkIndex, m ? 1 : 0, nullptr, nullptr));
    notify();
}

void PulseMixer::queryDefaultSink() {
    pa_operation_unref(pa_context_get_server_info(ctx, serverInfoCb, this));
}

void PulseMixer::updateSink(const pa_sink_info* info) {
    sinkIndex = info->index;
    channels = info->volume.channels ? info->volume.channels : 2;
    vol = (float)pa_cvolume_avg(&info->volume) / (float)PA_VOLUME_NORM;
    mute = info->mute != 0;
    notify();
}

void PulseMixer::notify() {
    forEach<Listener>(c->mixerListeners, [](Listener& listener) {
        listener.onListen();
    });

    c->scene->needsFrame = true;
}

namespace {
    void stateCb(pa_context* ctx, void* data) {
        auto* m = (PulseMixer*)data;

        switch (pa_context_get_state(ctx)) {
            case PA_CONTEXT_READY:
                m->ready = true;
                pa_context_set_subscribe_callback(ctx, subscribeCb, m);
                pa_operation_unref(pa_context_subscribe(ctx, (pa_subscription_mask_t)(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER), nullptr, nullptr));
                m->queryDefaultSink();

                break;
            case PA_CONTEXT_FAILED:
            case PA_CONTEXT_TERMINATED:
                m->ready = false;

                break;
            default:
                break;
        }
    }

    void subscribeCb(pa_context*, pa_subscription_event_type_t, u32, void* data) {
        // any sink or server change: the default sink or its volume may have
        // moved, re-read from the top
        ((PulseMixer*)data)->queryDefaultSink();
    }

    void serverInfoCb(pa_context* ctx, const pa_server_info* info, void* data) {
        auto* m = (PulseMixer*)data;

        if (info && info->default_sink_name) {
            pa_operation_unref(pa_context_get_sink_info_by_name(ctx, info->default_sink_name, sinkInfoCb, m));
        }
    }

    void sinkInfoCb(pa_context*, const pa_sink_info* info, int eol, void* data) {
        if (eol || !info) {
            return;
        }

        ((PulseMixer*)data)->updateSink(info);
    }
}

Mixer* MixerPulse::create(Composer& c) {
    auto* m = c.pool->make<PulseMixer>(c);

    if (!m->ctx || pa_context_get_state(m->ctx) == PA_CONTEXT_FAILED) {
        return nullptr;
    }

    *(c.log) << "imway: pulse mixer (pulseaudio/pipewire)"_sv << endL;

    return m;
}

#else // no libpulse

struct Composer;
struct Mixer;

Mixer* MixerPulse::create(Composer&) {
    return nullptr;
}

#endif
