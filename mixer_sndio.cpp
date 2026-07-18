#include "composer.h"
#include "mixer.h"
#include "mixer_sndio.h"
#include "pooled.h"
#include "pooled_ev.h"

#if __has_include(<sndio.h>)

#include "scene.h"
#include "util.h"

#include <math.h>
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

SndioMixer::SndioMixer(Composer& comp, struct sioctl_hdl* h)
    : c(&comp)
    , hdl(h)
{
    // handle guard first: LIFO stops the watcher before sioctl_close
    pooledGuard(*comp.pool, [h] {
        sioctl_close(h);
    });
    io = PooledEvIo::create(*comp.pool, comp.loop);
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

    v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;

    unsigned raw = (unsigned)lroundf(v * (float)levelMax);

    if (raw == level) {
        return;
    }

    level = raw;
    sioctl_setval(hdl, (unsigned)levelAddr, raw);
    rearm();
    notify();
}

bool SndioMixer::muted() {
    return muteAddr >= 0 ? mute != 0 : level == 0 && softSaved > 0.f;
}

void SndioMixer::setMuted(bool m) {
    if (muteAddr >= 0) {
        if ((unsigned)m == mute) {
            return;
        }

        mute = m;
        sioctl_setval(hdl, (unsigned)muteAddr, m);
        rearm();
        notify();

        return;
    }

    // no mute control: park the level at zero and remember the way back
    if (m && level > 0) {
        softSaved = volume();
        setVolume(0.f);
    } else if (!m && softSaved > 0.f) {
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

    ev_io_init(io, ioCb, pfd.fd, ev);
    io->data = this;
    ev_io_start(c->loop, io);
}

void SndioMixer::notify() {
    for (MixerListener* l : c->mixerListeners) {
        l->volumeChanged();
    }

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

void SndioMixer::val(unsigned addr, unsigned v) {
    if ((int)addr == levelAddr && v != level) {
        level = v;
        notify();
    } else if ((int)addr == muteAddr && v != mute) {
        mute = v;
        notify();
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
            sysE << "imway: sndiod went away, volume control disabled"_sv << endL;
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
        sysE << "imway: sndiod unreachable, volume control disabled"_sv << endL;

        return nullptr;
    }

    SndioMixer* m = c.pool->make<SndioMixer>(c, hdl);

    sysO << "imway: sndio mixer, level "_sv << (i64)m->levelAddr << ", mute "_sv << (i64)m->muteAddr << endL;

    return m;
}

#else // no sndio

Mixer* MixerSndio::create(Composer&) {
    return nullptr;
}

#endif
