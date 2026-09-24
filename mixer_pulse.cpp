#include "mixer_pulse.h"

#include "log.h"
#include "mixer.h"
#include "pooled.h"
#include "composer.h"
#include "listener.h"
#include "intr_list.h"
#include "ev_watch.h"

#include <std/mem/small_obj_allocator.h>

#if __has_include(<pulse/pulseaudio.h>)

    #include "scene.h"
    #include "util.h"

    #include <time.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/time.h>

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
    pa_mainloop_api* api = nullptr;
    pa_io_event_cb_t cb = nullptr;
    pa_io_event_destroy_cb_t destroy = nullptr;
    void* userdata = nullptr;
};

struct pa_time_event {
    ev_timer timer{};
    pa_mainloop_api* api = nullptr;
    pa_time_event_cb_t cb = nullptr;
    pa_time_event_destroy_cb_t destroy = nullptr;
    void* userdata = nullptr;
};

struct pa_defer_event {
    ev_prepare prep{};
    pa_mainloop_api* api = nullptr;
    pa_defer_event_cb_t cb = nullptr;
    pa_defer_event_destroy_cb_t destroy = nullptr;
    void* userdata = nullptr;
    bool started = false;
};

namespace {
    // the context behind pa_mainloop_api::userdata: the composer, whose loop
    // the events ride and from whose small-object allocator they come (the
    // dbus_conn WatchBox pattern), not the raw heap. Kept trivially
    // destructible: the pool preserves the storage until its own death, so
    // PulseMixer's pooledGuard teardown — which runs after the impl dies and
    // frees the surviving events through io_free/time_free/defer_free —
    // still releases safely
    struct PulseApi {
        pa_mainloop_api api{};
        Composer* comp = nullptr;

        PulseApi(Composer& c);
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
        pa_io_event* e = apiCtx(a)->comp->alloc->make<pa_io_event>();

        e->api = a;
        e->cb = cb;
        e->userdata = userdata;
        evIoInit(&e->io, ioEvCb, fd, toEv(f));
        e->io.data = e;
        ev_io_start(apiCtx(e->api)->comp->loop, &e->io);

        return e;
    }

    void ioEnable(pa_io_event* e, pa_io_event_flags_t f) {
        ev_io_stop(apiCtx(e->api)->comp->loop, &e->io);
        evIoSet(&e->io, e->io.fd, toEv(f));
        ev_io_start(apiCtx(e->api)->comp->loop, &e->io);
    }

    void ioFree(pa_io_event* e) {
        ev_io_stop(apiCtx(e->api)->comp->loop, &e->io);

        if (e->destroy) {
            e->destroy(e->api, e, e->userdata);
        }

        apiCtx(e->api)->comp->alloc->release(e);
    }

    void ioSetDestroy(pa_io_event* e, pa_io_event_destroy_cb_t cb) {
        e->destroy = cb;
    }

    // the seconds until an absolute pulse time. A context on a loop that
    // is not pulse's own (use_rtclock is false for it) hands out
    // wall-clock times: read against the monotonic clock, one lies decades
    // ahead and the timer, a reply timeout among them, never fires
    double delayUntil(const struct timeval& tv) {
        struct timespec now{};

        clock_gettime(CLOCK_REALTIME, &now);

        double target = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
        double n = (double)now.tv_sec + (double)now.tv_nsec / 1e9;
        double d = target - n;

        return d < 0. ? 0. : d;
    }

    void timeEvCb(struct ev_loop*, ev_timer* w, int) {
        auto* e = (pa_time_event*)w->data;
        struct timeval tv{};

        e->cb(e->api, e, &tv, e->userdata);
    }

    // a null time disarms the event, as on pulse's own mainloop; it is
    // not a time that has already passed
    void timeRestart(pa_time_event* e, const struct timeval* tv) {
        ev_timer_stop(apiCtx(e->api)->comp->loop, &e->timer);

        if (!tv) {
            return;
        }

        evTimerSet(&e->timer, delayUntil(*tv), 0.);
        ev_timer_start(apiCtx(e->api)->comp->loop, &e->timer);
    }

    pa_time_event* timeNew(pa_mainloop_api* a, const struct timeval* tv, pa_time_event_cb_t cb, void* userdata) {
        pa_time_event* e = apiCtx(a)->comp->alloc->make<pa_time_event>();

        e->api = a;
        e->cb = cb;
        e->userdata = userdata;
        evTimerInit(&e->timer, timeEvCb, 0., 0.);
        e->timer.data = e;
        timeRestart(e, tv);

        return e;
    }

    void timeFree(pa_time_event* e) {
        ev_timer_stop(apiCtx(e->api)->comp->loop, &e->timer);

        if (e->destroy) {
            e->destroy(e->api, e, e->userdata);
        }

        apiCtx(e->api)->comp->alloc->release(e);
    }

    void timeSetDestroy(pa_time_event* e, pa_time_event_destroy_cb_t cb) {
        e->destroy = cb;
    }

    void deferPrepCb(struct ev_loop*, ev_prepare* w, int) {
        auto* e = (pa_defer_event*)w->data;

        e->cb(e->api, e, e->userdata);
    }

    pa_defer_event* deferNew(pa_mainloop_api* a, pa_defer_event_cb_t cb, void* userdata) {
        pa_defer_event* e = apiCtx(a)->comp->alloc->make<pa_defer_event>();

        e->api = a;
        e->cb = cb;
        e->userdata = userdata;
        evPrepareInit(&e->prep, deferPrepCb);
        e->prep.data = e;
        ev_prepare_start(apiCtx(e->api)->comp->loop, &e->prep);
        e->started = true;

        return e;
    }

    void deferEnable(pa_defer_event* e, int b) {
        if (b && !e->started) {
            ev_prepare_start(apiCtx(e->api)->comp->loop, &e->prep);
            e->started = true;
        } else if (!b && e->started) {
            ev_prepare_stop(apiCtx(e->api)->comp->loop, &e->prep);
            e->started = false;
        }
    }

    void deferFree(pa_defer_event* e) {
        if (e->started) {
            ev_prepare_stop(apiCtx(e->api)->comp->loop, &e->prep);
        }

        if (e->destroy) {
            e->destroy(e->api, e, e->userdata);
        }

        apiCtx(e->api)->comp->alloc->release(e);
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

PulseApi::PulseApi(Composer& c)
    : comp(&c)
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
    papi = comp.pool->make<PulseApi>(comp);
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

// both callers, the volume keys and the settings slider, clamp to 0..1
void PulseMixer::setVolume(float v) {
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
    // a sink's volume carries one entry per channel, and a sink has at
    // least one
    channels = info->volume.channels;
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
                *(m->c->log) << "imway: pulse connection failed: "_sv << StringView(pa_strerror(pa_context_errno(ctx))) << ", volume control disabled"_sv << endL;
                m->ready = false;

                break;
            // TERMINATED follows only our own disconnect, which silences
            // this callback first
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

    // libpulse hands out each sink with eol 0 and a record, then an eol
    // (1 at the end, -1 on error) without one
    void sinkInfoCb(pa_context*, const pa_sink_info* info, int eol, void* data) {
        if (eol) {
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

#ifdef IMWAY_FOR_TESTS
// Held against pulse's own loop: an event freed with a destroy hook set
// calls it once, with its userdata; a null time disarms a timer; quit
// leaves a loop pulse does not own running. Counted, not branched on: a
// broken part shows as a nonzero count.
int MixerPulse::mainloopConformance(Composer& c) {
    PulseApi ctx(c);

    fillApi(ctx);

    pa_mainloop_api* api = &ctx.api;
    int destroyed = 0;
    int failed = 0;
    int fds[2] = {-1, -1};

    failed += pipe2(fds, O_CLOEXEC) != 0;

    pa_io_event* io = api->io_new(api, fds[0], PA_IO_EVENT_OUTPUT, [](pa_mainloop_api*, pa_io_event*, int, pa_io_event_flags_t, void*) {}, &destroyed);

    api->io_enable(io, PA_IO_EVENT_INPUT);
    api->io_set_destroy(io, [](pa_mainloop_api*, pa_io_event*, void* count) {
        ++*(int*)count;
    });
    api->io_free(io);
    failed += destroyed != 1;

    struct timeval later{};

    gettimeofday(&later, nullptr);
    later.tv_sec += 60;

    pa_time_event* timer = api->time_new(api, &later, [](pa_mainloop_api*, pa_time_event*, const struct timeval*, void*) {}, &destroyed);

    api->time_restart(timer, nullptr);
    failed += ev_is_active(&timer->timer) != 0;
    api->time_set_destroy(timer, [](pa_mainloop_api*, pa_time_event*, void* count) {
        ++*(int*)count;
    });
    api->time_free(timer);
    failed += destroyed != 2;

    pa_defer_event* defer = api->defer_new(api, [](pa_mainloop_api*, pa_defer_event*, void*) {}, &destroyed);

    api->defer_enable(defer, 0);
    api->defer_set_destroy(defer, [](pa_mainloop_api*, pa_defer_event*, void* count) {
        ++*(int*)count;
    });
    api->defer_free(defer);
    failed += destroyed != 3;

    api->quit(api, 0);
    close(fds[0]);
    close(fds[1]);

    return failed;
}
#endif

#else // no libpulse

struct Composer;
struct Mixer;

Mixer* MixerPulse::create(Composer&) {
    return nullptr;
}

#ifdef IMWAY_FOR_TESTS
int MixerPulse::mainloopConformance(Composer&) {
    return -1;
}
#endif

#endif
