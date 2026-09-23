#include "mixer_sndio.h"

#include "log.h"
#include "mixer.h"
#include "pooled.h"
#include "composer.h"
#include "listener.h"
#include "intr_list.h"
#include "ev_watch.h"

#if __has_include(<sndio.h>)

    #include "scene.h"
    #include "util.h"

    #include <math.h>
    #include <string.h>
    #include <poll.h>

    #include <ev.h>

    #include <sndio.h>

    #include <std/ios/sys.h>
    #include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    void ioCb(struct ev_loop*, ev_io* w, int);
    void onDesc(void* arg, struct sioctl_desc* d, int val);
    void onVal(void* arg, unsigned addr, unsigned val);

    // the values this client wrote to one control and sndiod has not yet
    // reported back. sndiod reports every change to every client, the
    // writer included, and it may skip intermediate values; an echo of a
    // write the compositor has since moved past is not news
    struct Writes {
        static constexpr size_t capacity = 32;

        unsigned values[capacity] = {};
        size_t count = 0;

        void sent(unsigned v);
        bool echoed(unsigned v);
    };

    // sndiod's server-level controls: output.level (NUM 0..maxval) and
    // output.mute (SW); per-app controls live in other groups and are not
    // our business
    struct SndioMixer: public Mixer {
        Composer* c = nullptr;
        struct sioctl_hdl* hdl = nullptr;
        ev_io* io = nullptr;

        int levelAddr = -1;
        int muteAddr = -1;
        unsigned levelMax = 127;
        unsigned level = 0;
        unsigned mute = 0;
        float softSaved = 0.f; // soft-mute stash when there is no mute control
        Writes levelWrites;
        Writes muteWrites;

        SndioMixer(Composer& comp, struct sioctl_hdl* h);

        float volume() override;
        void setVolume(float v) override;
        bool muted() override;
        void setMuted(bool m) override;

        void rearm();
        void notify();
        void desc(struct sioctl_desc* d, int val);
        void val(unsigned addr, unsigned v);
    };
}

// the oldest write gives way when a burst outruns sndiod's reports
void Writes::sent(unsigned v) {
    if (count == capacity) {
        memmove(values, values + 1, (capacity - 1) * sizeof(values[0]));
        count--;
    }

    values[count++] = v;
}

// a reported value that is one of the writes in flight is their echo: it
// and every write before it are done. Any other value came from outside
// and supersedes them all
bool Writes::echoed(unsigned v) {
    for (size_t i = 0; i < count; i++) {
        if (values[i] == v) {
            memmove(values, values + i + 1, (count - i - 1) * sizeof(values[0]));
            count -= i + 1;

            return true;
        }
    }

    count = 0;

    return false;
}

SndioMixer::SndioMixer(Composer& comp, struct sioctl_hdl* h)
    : c(&comp)
    , hdl(h)
{
    // handle guard first: LIFO stops the watcher before sioctl_close
    pooledGuard(*comp.pool, [h] {
        sioctl_close(h);
    });
    io = comp.pool->make<ev_io>();
    struct ev_loop* heldLoop = comp.loop;
    ev_io* heldIo = io;

    pooledGuard(*comp.pool, [heldLoop, heldIo] {
        if (ev_is_active(heldIo)) {
            ev_io_stop(heldLoop, heldIo);
        }
    });
    sioctl_ondesc(hdl, onDesc, this);
    sioctl_onval(hdl, onVal, this);
    rearm();
}

float SndioMixer::volume() {
    return levelMax ? (float)level / (float)levelMax : 0.f;
}

void SndioMixer::setVolume(float v) {
    if (levelAddr < 0) {
        return;
    }

    // both callers, the volume keys and the settings slider, clamp to 0..1,
    // and the soft mute only parks at zero and restores a stashed level
    unsigned raw = (unsigned)lroundf(v * (float)levelMax);

    if (raw == level) {
        return;
    }

    level = raw;
    levelWrites.sent(raw);
    sioctl_setval(hdl, (unsigned)levelAddr, raw);
    rearm();
    notify();
}

bool SndioMixer::muted() {
    return muteAddr >= 0 ? mute != 0 : level == 0 && softSaved > 0.f;
}

// both callers (the mute key, the settings checkbox) ask for the opposite
// of muted(): with a mute control that is never its current value, and
// without one an unmute always has a saved level to go back to
void SndioMixer::setMuted(bool m) {
    if (muteAddr >= 0) {
        mute = m;
        muteWrites.sent(m);
        sioctl_setval(hdl, (unsigned)muteAddr, m);
        rearm();
        notify();

        return;
    }

    // no mute control: park the level at zero and remember the way back
    if (m && level > 0) {
        softSaved = volume();
        setVolume(0.f);
    } else if (!m) {
        float v = softSaved;

        softSaved = 0.f;
        setVolume(v);
    }
}

void SndioMixer::rearm() {
    struct pollfd pfd;

    if (sioctl_pollfd(hdl, &pfd, POLLIN) != 1) {
        return;
    }

    int ev = (pfd.events & POLLIN ? EV_READ : 0) | (pfd.events & POLLOUT ? EV_WRITE : 0);

    if (ev_is_active(io)) {
        ev_io_stop(c->loop, io);
    }

    evIoInit(io, ioCb, pfd.fd, ev);
    io->data = this;
    ev_io_start(c->loop, io);
}

void SndioMixer::notify() {
    forEach<Listener>(c->mixerListeners, [](Listener& listener) {
        listener.onListen();
    });

    c->scene->needsFrame = true;
}

void SndioMixer::desc(struct sioctl_desc* d, int v) {
    if (!d) {
        return; // end of the initial enumeration
    }

    if (StringView(d->group).empty() && StringView(d->node0.name) == "output"_sv) {
        if (d->type == SIOCTL_NUM && StringView(d->func) == "level"_sv) {
            levelAddr = (int)d->addr;
            levelMax = d->maxval ? d->maxval : 1;
            level = (unsigned)v;
        } else if (d->type == SIOCTL_SW && StringView(d->func) == "mute"_sv) {
            muteAddr = (int)d->addr;
            mute = (unsigned)v;
        } else if (d->type == SIOCTL_NONE) {
            if ((int)d->addr == levelAddr) {
                levelAddr = -1;
            }

            if ((int)d->addr == muteAddr) {
                muteAddr = -1;
            }
        }
    }
}

// an echo of a write the compositor has since moved past must not undo
// it: the next volume step would start from the stale level
void SndioMixer::val(unsigned addr, unsigned v) {
    if ((int)addr == levelAddr) {
        if (!levelWrites.echoed(v) && v != level) {
            level = v;
            notify();
        }
    } else if ((int)addr == muteAddr) {
        if (!muteWrites.echoed(v) && v != mute) {
            mute = v;
            notify();
        }
    }
}

namespace {
    void ioCb(struct ev_loop*, ev_io* w, int) {
        auto* m = (SndioMixer*)w->data;
        struct pollfd pfd;

        if (sioctl_pollfd(m->hdl, &pfd, POLLIN) == 1) {
            pfd.revents = (short)((w->events & EV_READ ? POLLIN : 0) | (w->events & EV_WRITE ? POLLOUT : 0));
            sioctl_revents(m->hdl, &pfd);
        }

        if (sioctl_eof(m->hdl)) {
            *(m->c->log) << "imway: sndiod went away, volume control disabled"_sv << endL;
            ev_io_stop(m->c->loop, m->io);
            m->levelAddr = -1;
            m->muteAddr = -1;

            return;
        }

        m->rearm();
    }

    void onDesc(void* arg, struct sioctl_desc* d, int val) {
        ((SndioMixer*)arg)->desc(d, val);
    }

    void onVal(void* arg, unsigned addr, unsigned val) {
        ((SndioMixer*)arg)->val(addr, val);
    }
}

Mixer* MixerSndio::create(Composer& c) {
    struct sioctl_hdl* hdl = sioctl_open(SIO_DEVANY, SIOCTL_READ | SIOCTL_WRITE, 1);

    if (!hdl) {
        *(c.log) << "imway: sndiod unreachable, volume control disabled"_sv << endL;

        return nullptr;
    }

    SndioMixer* m = c.pool->make<SndioMixer>(c, hdl);

    *(c.log) << "imway: sndio mixer, level "_sv << (i64)m->levelAddr << ", mute "_sv << (i64)m->muteAddr << endL;

    return m;
}

#else // no sndio

Mixer* MixerSndio::create(Composer&) {
    return nullptr;
}

#endif
